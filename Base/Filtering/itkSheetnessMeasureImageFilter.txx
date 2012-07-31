/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __itkSheetnessMeasureImageFilter_txx
#define __itkSheetnessMeasureImageFilter_txx

#include "itkSheetnessMeasureImageFilter.h"
#include "itkImageRegionIterator.h"
#include "vnl/vnl_math.h"

namespace itk
{
/**
 * Constructor
 */
template< typename TPixel >
SheetnessMeasureImageFilter< TPixel >
::SheetnessMeasureImageFilter()
{
  m_Alpha = 0.5;
  m_Beta = 2.0;
  m_Cfactor = 2.0;
  m_DetectBrightSheets = true;

  // Hessian( Image ) = Jacobian( Gradient ( Image ) )  is symmetric
  m_SymmetricEigenValueFilter = EigenAnalysisFilterType::New();
  m_SymmetricEigenValueFilter->SetDimension(ImageDimension);
  m_SymmetricEigenValueFilter->OrderEigenValuesBy(
    EigenAnalysisFilterType::FunctorType::OrderByValue);
}

template< typename TPixel >
void
SheetnessMeasureImageFilter< TPixel >
::GenerateData()
{
  itkDebugMacro(<< "SheetnessMeasureImageFilter generating data ");

  m_SymmetricEigenValueFilter->SetInput( this->GetInput() );

  typename OutputImageType::Pointer output = this->GetOutput();

  typedef typename EigenAnalysisFilterType::OutputImageType
  EigenValueOutputImageType;

  m_SymmetricEigenValueFilter->Update();

  const typename EigenValueOutputImageType::ConstPointer eigenImage =
    m_SymmetricEigenValueFilter->GetOutput();

  // walk the region of eigen values and get the vesselness measure
  EigenValueArrayType                                   eigenValue;
  ImageRegionConstIterator< EigenValueOutputImageType > it;
  it = ImageRegionConstIterator< EigenValueOutputImageType >(
    eigenImage, eigenImage->GetRequestedRegion() );
  ImageRegionIterator< OutputImageType > oit;
  this->AllocateOutputs();
  oit = ImageRegionIterator< OutputImageType >( output,
                                                output->GetRequestedRegion() );
  oit.GoToBegin();
  it.GoToBegin();
  while ( !it.IsAtEnd() )
    {
    // Get the eigen value
    eigenValue = it.Get();

    double sheetness = 0.0;

    double a1 = static_cast<double>( eigenValue[0] );
    double a2 = static_cast<double>( eigenValue[1] );
    double a3 = static_cast<double>( eigenValue[2] );

    double l1 = vnl_math_abs( a1 );
    double l2 = vnl_math_abs( a2 );
    double l3 = vnl_math_abs( a3 );

    //
    // Sort the values by their absolute value.
    // At the end of the sorting we should have
    //
    //          l1 <= l2 <= l3
    //
    if( l2 > l3 )
      {
      double tmpl = l3;
      l3 = l2;
      l2 = tmpl;
      double tmpa = a3;
      a3 = a2;
      a2 = tmpa;
      }

    if( l1 > l2 )
      {
      double tmp = l1;
      l1 = l2;
      l2 = tmp;
      double tmpa = a1;
      a1 = a2;
      a2 = tmpa;
      }

    if( l2 > l3 )
      {
      double tmp = l3;
      l3 = l2;
      l2 = tmp;
      double tmpa = a3;
      a3 = a2;
      a2 = tmpa;
      }

    if( this->m_DetectBrightSheets )
      {
      if( a3 > 0.0 )
        {
        oit.Set(NumericTraits< OutputPixelType >::Zero);
        ++it;
        ++oit;
        continue;
        }
      }
    else
      {
      if( a3 < 0.0 )
        {
        oit.Set(NumericTraits< OutputPixelType >::Zero);
        ++it;
        ++oit;
        continue;
        }
      }

    //
    // Avoid divisions by zero (or close to zero)
    //
    if( static_cast<double>( l3 ) < vnl_math::eps )
      {
      oit.Set(NumericTraits< OutputPixelType >::Zero);
      }
    else
      {
      const double Rs = l2 / l3;
      const double Rb = vnl_math_abs( l3 + l3 - l2 - l1 ) / l3;
      const double Rn = vcl_sqrt( l3*l3 + l2*l2 + l1*l1 );

      sheetness  =         vcl_exp( - ( Rs * Rs ) / ( 2.0 * m_Alpha * m_Alpha ) );
      sheetness *= ( 1.0 - vcl_exp( - ( Rb * Rb ) / ( 2.0 * m_Beta * m_Beta ) ) );
      sheetness *= ( 1.0 - vcl_exp( - ( Rn * Rn ) / ( 2.0 * m_Cfactor     * m_Cfactor     ) ) );
      oit.Set(static_cast< OutputPixelType >( sheetness));
      }

    ++it;
    ++oit;
    }
}

template< typename TPixel >
void
SheetnessMeasureImageFilter< TPixel >
::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);

  os << indent << "Alpha1: " << m_Alpha << std::endl;
  os << indent << "Beta: " << m_Beta << std::endl;
  os << indent << "Cfactor: " << m_Cfactor << std::endl;
  if( m_DetectBrightSheets )
    {
    os << indent << "DetectBrightSheets: true" << std::endl;
    }
  else
    {
    os << indent << "DetectBrightSheets: false" << std::endl;
    }
}

} // end namespace itk

#endif