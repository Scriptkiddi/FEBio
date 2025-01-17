/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2021 University of Utah, The Trustees of Columbia University in
the City of New York, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/



#include "stdafx.h"
#include "FESolubConst.h"

//-----------------------------------------------------------------------------
// define the material parameters
BEGIN_FECORE_CLASS(FESolubConst, FESoluteSolubility)
	ADD_PARAMETER(m_solub, FE_RANGE_GREATER_OR_EQUAL(0.0), "solub");
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
//! Constructor. 
FESolubConst::FESolubConst(FEModel* pfem) : FESoluteSolubility(pfem)
{
	m_solub = 1;
}

//-----------------------------------------------------------------------------
//! Solubility
double FESolubConst::Solubility(FEMaterialPoint& mp)
{
	// --- constant solubility ---
	
	return m_solub;
}

//-----------------------------------------------------------------------------
//! Tangent of solubility with respect to strain
double FESolubConst::Tangent_Solubility_Strain(FEMaterialPoint &mp)
{
	return 0;
}

//-----------------------------------------------------------------------------
//! Tangent of solubility with respect to concentration
double FESolubConst::Tangent_Solubility_Concentration(FEMaterialPoint &mp, const int isol)
{
	return 0;
}

//-----------------------------------------------------------------------------
//! Cross derivative of solubility with respect to strain and concentration
double FESolubConst::Tangent_Solubility_Strain_Concentration(FEMaterialPoint &mp, const int isol)
{
	return 0;
}

//-----------------------------------------------------------------------------
//! Second derivative of solubility with respect to strain
double FESolubConst::Tangent_Solubility_Strain_Strain(FEMaterialPoint &mp)
{
	return 0;
}

//-----------------------------------------------------------------------------
//! Second derivative of solubility with respect to concentration
double FESolubConst::Tangent_Solubility_Concentration_Concentration(FEMaterialPoint &mp, const int isol, const int jsol)
{
	return 0;
}
