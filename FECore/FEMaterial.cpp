// FEMaterial.cpp: implementation of the FEMaterial class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "FEMaterial.h"
#include <math.h>
#include <stdarg.h>
#include "FEModel.h"

//-----------------------------------------------------------------------------
MaterialError::MaterialError(const char* szfmt, ...)
{
	// get a pointer to the argument list
	va_list	args;

	// print to file
	va_start(args, szfmt);
	vsprintf(m_szerr, szfmt, args);
	va_end(args);
}

//-----------------------------------------------------------------------------
FEMaterial::FEMaterial()
{
	static int n = 1;
	m_szname[0] = 0;
	m_nID = -1;
}

//-----------------------------------------------------------------------------
FEMaterial::~FEMaterial()
{
}

//-----------------------------------------------------------------------------
//! Store the material data to the archive
void FEMaterial::Serialize(DumpFile &ar)
{
	if (ar.IsSaving())
	{
		ar << m_nID;

		// store all parameters
		FEParameterList& pl = GetParameterList();
		int n = pl.Parameters();
		ar << n;
		list<FEParam>::iterator it = pl.first();
		for (int j=0; j<n; ++j, ++it)
		{
			// store the value
			switch (it->m_itype)
			{
			case FE_PARAM_INT    : ar << it->value<int   >(); break;
			case FE_PARAM_BOOL   : ar << it->value<bool  >(); break;
			case FE_PARAM_DOUBLE : ar << it->value<double>(); break;
			case FE_PARAM_VEC3D  : ar << it->value<vec3d >(); break;
			case FE_PARAM_DOUBLEV: { for (int k=0; k<it->m_ndim; ++k) ar << it->pvalue<double>()[k]; } break;
			case FE_PARAM_INTV   : { for (int k=0; k<it->m_ndim; ++k) ar << it->pvalue<int   >()[k]; } break;
			default:
				assert(false);
			}

			// store parameter loadcurve data
			ar << it->m_nlc;
		}
	}
	else
	{
		ar >> m_nID;

		FEParameterList& pl = GetParameterList();
		int n = 0;
		ar >> n;
		assert(n == pl.Parameters());
		list<FEParam>::iterator it = pl.first();
		for (int j=0; j<n; ++j, ++it)
		{
			// read the value
			switch (it->m_itype)
			{
			case FE_PARAM_INT    : ar >> it->value<int   >(); break;
			case FE_PARAM_BOOL   : ar >> it->value<bool  >(); break;
			case FE_PARAM_DOUBLE : ar >> it->value<double>(); break;
			case FE_PARAM_VEC3D  : ar >> it->value<vec3d >(); break;
			case FE_PARAM_DOUBLEV: { for (int k=0; k<it->m_ndim; ++k) ar >> it->pvalue<double>()[k]; } break;
			case FE_PARAM_INTV   : { for (int k=0; k<it->m_ndim; ++k) ar >> it->pvalue<int   >()[k]; } break;
			default:
				assert(false);
			}

			// read parameter data
			ar >> it->m_nlc;
		}
	}
}

//-----------------------------------------------------------------------------
// Derivative of stress w.r.t. solute concentration at material point
// Set this to zero by default because elasticity problems do not require it
mat3ds FESolidMaterial::Tangent_Concentration(FEMaterialPoint& pt, const int isol)
{
	return mat3ds(0,0,0,0,0,0);
}

//-----------------------------------------------------------------------------
// Material parameters for the FENestedMaterial
BEGIN_PARAMETER_LIST(FENestedMaterial, FEMaterial)
	ADD_PARAMETER(m_nBaseMat, FE_PARAM_INT, "solid_id");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
void FENestedMaterial::Serialize(DumpFile &ar)
{
	// serialize base class
	FESolidMaterial::Serialize(ar);

	// serialize nested material data
	if (ar.IsSaving())
	{
		ar << m_nBaseMat;
		assert(m_pBase && (m_pBase->GetID() == m_nBaseMat));
	}
	else
	{
		ar >> m_nBaseMat;

		// We can't set the base material here since it may not have been loaded yet
		m_pBase = 0;
	}
}

//=============================================================================
// FEMultiMaterial
//=============================================================================

//-----------------------------------------------------------------------------
int FEMultiMaterial::FindComponent(const char* sz, int nid)
{
	for (int i=0; i<(int) m_Mat.size(); ++i)
	{
		Property* p = m_Mat[i];
		if ((strcmp(p->GetName(), sz) == 0) && (p->GetID() == nid)) return i;
	}
	return -1;
}
