/*=========================================================================

Library:   TubeTK

Copyright 2010 Kitware Inc. 28 Corporate Drive,
Clifton Park, NY, 12065, USA.

All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

=========================================================================*/


#include "tubeMessage.h"
#include "tubeCLIFilterWatcher.h"
#include "tubeCLIProgressReporter.h"
#include "itkTimeProbesCollectorBase.h"

#include "itkMatrix.h"
#include "ShortestPathKernel.h"
#include "WLSubtreeKernel.h"

#include "TubeGraphKernelCLP.h"

enum
  {
  GK_SPKernel = 0,
  GK_WLKernel = 1
  };


/**
 * Reads a list of input graphs from JSON file 'fileName' and
 * stores the full path's the the graph files in 'list', as well
 * as the label (i.e., class assignment) information in 'labels'.
 */
void readGraphList(const std::string &fileName,
                   std::vector<std::string> &list,
                   std::vector<int> &labels)
{
  list.clear();
  try
    {
    boost::property_tree::ptree pt;
    read_json(fileName, pt);

    int nGraphs = boost::lexical_cast<int>( pt.get<std::string>("nGraphs") );

    int graphCount = 0;
    BOOST_FOREACH( boost::property_tree::ptree::value_type &v, pt.get_child("graphList") )
      {
      const std::string &file = boost::lexical_cast<std::string>( v.second.data() );
      list.push_back( file );
      ++graphCount;
      }

    int labelCount = 0;
    BOOST_FOREACH( boost::property_tree::ptree::value_type &v, pt.get_child("labels") )
      {
      int label = boost::lexical_cast<int>( v.second.data() );
      labels.push_back( label );
      ++labelCount;
      }
    assert( graphCount == nGraphs );
    assert( labelCount == nGraphs );
    }
  catch( std::exception &e )
    {
    tube::FmtErrorMessage("Error reading JSON graph file %s (Msg: %s)",
      fileName.c_str(), e.what());
    throw std::exception();
    }
}


/**
 * Write a VNL matrix (the kernel matrix) 'K' to the file 'baseFileName'
 * in binary format (double).
 */
void writeKernel(const std::string &baseFileName, const vnl_matrix<double> &K)
{
  std::string outFileName = baseFileName + ".bin";
  std::ofstream ofs;

  ofs.open( outFileName.c_str(), std::ios::binary | std::ios::out );
  if( !ofs )
    {
    tube::FmtErrorMessage("Could not open kernel matrix %s for writing!",
      outFileName.c_str());
    throw std::exception();
    }
  ofs.write( (const char *)K.data_block(), K.size()*sizeof(double) );
  if( ofs.bad() )
    {
    tube::FmtErrorMessage("Could not write kernel matrix %s!",
      outFileName.c_str());
    throw std::exception();
    }
  ofs.close();
  if( ofs.fail() )
    {
    tube::FmtErrorMessage("Could not close kernel matrix file %s!",
      outFileName.c_str());
    throw std::exception();
    }
}


/**
 * Write a VNL matrix (the kernel matrix) 'K' to the file
 * 'baseFileName' in LIBSVM compatible format. 'labels' is
 * used to augment the kernel with label information, such
 * that it can be immediately used with LIBSVM's svm-train
 * binary.
 */
void writeKernelLibSVM(const std::string &baseFileName,
                       const vnl_matrix<double> &K,
                       const std::vector<int> &labels)
{
  std::string outFileName = baseFileName + ".libsvm";
  std::ofstream ofs;
  ofs.open( outFileName.c_str(), std::ios::binary | std::ios::out );
  if( !ofs )
    {
    tube::FmtErrorMessage("Could not open kernel matrix %s for writing!",
      outFileName.c_str());
    throw std::exception();
    }

  assert( K.rows() == labels.size() );

  for(unsigned int r = 0; r < K.rows(); ++r)
    {
    ofs << labels[r] << " " << "0:" << r+1 << " ";
    for(unsigned int c = 0; c < K.cols()-1; ++c)
      {
      ofs << c+1 << ":" << K[r][c] << " ";
      }
    ofs << K.cols() << ":" << K[r][K.cols()-1] << std::endl;
    }
  ofs.close();
  if( ofs.fail() )
    {
    tube::FmtErrorMessage("Could not close kernel matrix file %s!",
      outFileName.c_str());
    throw std::exception();
    }
}


/** Check if 'fileName' exists */
bool fileExists(const std::string &fileName)
{
  std::ifstream file( fileName.c_str() );
  if( !file.good() )
    {
    return false;
    }
  return true;
}


/**
 * Load a graph file (as adjacency matrix) from disk. We use
 * GraphKernel's functionality for that. Further, we try to load
 * a vertex label file if it exists. This file specifies the
 * vertex labelings to use. The convention is that the vertex
 * label file should have the same name as the adjaceny matrix
 * file + the suffix '.vertexLabel'
 */
tube::GraphKernel::GraphType loadGraph(std::string graphFile,
  tube::GraphKernel::DefaultNodeLabelingType defNodeLabel = tube::GraphKernel::LABEL_BY_NUM)
{
  const char * labelFile = 0;
  std::string labelFileStr = graphFile + ".vertexLabel";
  if( fileExists(labelFileStr) )
    {
    labelFile = labelFileStr.c_str();
    }
  return tube::GraphKernel::GraphFromAdjFile(
    graphFile.c_str(), labelFile, defNodeLabel );
}


int main(int argc, char **argv)
{
  PARSE_ARGS;

  try
    {
    switch( argGraphKernelType )
      {
      case GK_WLKernel:
      case GK_SPKernel:
        break;
      default:
        tube::ErrorMessage("Unsupported kernel!");
        return EXIT_FAILURE;
      }

    /*
     *
     * Read graph lists 'listA' and 'listB' and fill class labels;
     * NOTE: The labels are currently unused, since classification
     * is done in the driver script.
     *
     */

    std::vector<std::string> listA, listB;
    std::vector<int> labelsB, labelsA;
    readGraphList( argGraphListA, listA, labelsA );
    readGraphList( argGraphListB, listB, labelsB );

    int N = listA.size();
    int M = listB.size();

    assert( N > 0 && M > 0 );

    tube::FmtDebugMessage( "Read N=%d entries from %s.", N, argGraphListA.c_str() );
    tube::FmtDebugMessage( "Read M=%d entries from %s.", M, argGraphListB.c_str() );

    vnl_matrix<double> K( N, M );
    K.fill(0.0);

    /*
     *
     * In case we use the Weisfeiler-Lehman kernel, we need to build
     * the label compression mapping beforehand. This means, we need
     * to load the graphs, compress labels and store the mappings.
     *
     * NOTE: We only need to do this for the first set of graphs, which
     * in case of training data, will determine the compression mapping.
     * In case of testing, the first list is still the list of training
     * graphs and the second list will use that mapping. TODO: Make the
     * list loadable from file/
     *
     */

    if( !tube::GraphKernel::IsValidDefaultNodeLabeling( argDefaultLabelType ) )
      {
      tube::ErrorMessage( "Labeling strategy not supported!" );
      return EXIT_FAILURE;
      }
    tube::GraphKernel::DefaultNodeLabelingType defLabelType =
      static_cast<tube::GraphKernel::DefaultNodeLabelingType>( argDefaultLabelType );

    tube::WLSubtreeKernel::LabelMapVectorType labelMap( argSubtreeHeight );
    int labelCount = 0;


    if( argGraphKernelType == GK_WLKernel )
      {
      for( int i = 0; i < N; ++i )
        {
        tube::FmtInfoMessage("Adding data from graph %s",
          listA[i].c_str());

        tube::GraphKernel::GraphType f = loadGraph( listA[i], defLabelType );
        tube::WLSubtreeKernel::UpdateLabelCompression( f,
                                                 labelMap,
                                                 labelCount,
                                                 argSubtreeHeight );
        }
      }


    /*
     *
     * Next, we build the kernel matrix K, where the K_ij-th entry
     * is the WL kernel value between the i-th graph of the first
     * (i.e., 'listA') list and the j-th graph of the second list
     * (i.e., 'listB').
     *
     */

    for( int i = 0; i < N; ++i )
      {
      tube::GraphKernel::GraphType f = loadGraph(listA[i], defLabelType );
      for( int j = 0; j < M; ++j )
        {
        tube::GraphKernel::GraphType g = loadGraph(listB[j], defLabelType );

        tube::FmtInfoMessage("Running kernel on graphs (%d,%d)",
          i,j);

        tube::GraphKernel *gk = 0;
        switch( argGraphKernelType )
          {
          case GK_SPKernel:
            {
            gk = new tube::ShortestPathKernel(f, g);
            K[i][j] = gk->Compute();
            break;
            }
          case GK_WLKernel:
            {
            gk = new tube::WLSubtreeKernel( f,
                                      g,
                                      labelMap,
                                      labelCount,
                                      argSubtreeHeight );

            K[i][j] = gk->Compute();
            break;
            }
          }
        }
      }

    /*
     *
     * Eventually, dump the kernel to disk - 1) in LIBSVM comp.
     * format (directly usable by svm-train) and 2) as a binary
     * kernel matrix that can be loaded in Python for instance
     * (e.g., for scikits-learn SVM)
     *
     */

    writeKernel( argOutputKernel, K );
    writeKernelLibSVM( argOutputKernel, K, labelsA );
    }
  catch(std::exception &e)
    {
    tube::ErrorMessage( "Exiting ..." );
    return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}
