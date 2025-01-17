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



#pragma once
#include <FECore/FEPrescribedBC.h>
#include "febiofluid_api.h"

//-----------------------------------------------------------------------------
//! FEFluidRotationalVelocity is a fluid surface that has a rotational
//! velocity prescribed on it.  This routine prescribes nodal velocities

class FEBIOFLUID_API FEFluidRotationalVelocity : public FEPrescribedNodeSet
{
public:
    //! constructor
    FEFluidRotationalVelocity(FEModel* pfem);

    //! initialization
    bool Init() override;

	//! serialization
	void Serialize(DumpStream& ar) override;
    
	// copy data from another class
	void CopyFrom(FEBoundaryCondition* pbc) override;

	// return nodal value
	void GetNodalValues(int nodelid, std::vector<double>& val) override;

	//! Set the dof list
	bool SetDofList(FEDofList& dofs) override;

private:
    double			m_w;        //!< angular speed
    vec3d           m_n;        //!< unit vector along axis of rotation
    vec3d           m_p;        //!< point on axis of rotation
    vector<vec3d>   m_r;        //!< nodal radial positions

    DECLARE_FECORE_CLASS();
};
