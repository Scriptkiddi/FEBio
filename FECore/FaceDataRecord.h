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
#include "FECoreBase.h"
#include "DataRecord.h"

class FESurface;
class FESurfaceElement;

//-----------------------------------------------------------------------------
//! This is the base class for a face data value.
class FECORE_API FEFaceLogData : public FECoreBase
{
	FECORE_SUPER_CLASS

public:
	FEFaceLogData(FEModel* fem);
	virtual ~FEFaceLogData();
	virtual double value(FESurfaceElement& el) = 0;
};

//-----------------------------------------------------------------------------
//! This class records surface data
class FECORE_API FaceDataRecord : public DataRecord
{
public:
	FaceDataRecord(FEModel* pfem, const char* szfile);
	double Evaluate(int item, int ndata) override;
	bool Initialize() override;
	void SetData(const char* sz)  override;
	void SelectAllItems() override;
	void SetSurface(FESurface* surf);
	void SetSurface(FESurface* surf, const std::vector<int>& items);
	int Size() const override;

private:
	FESurface*	m_surface;
	vector<FEFaceLogData*>	m_Data;
};
