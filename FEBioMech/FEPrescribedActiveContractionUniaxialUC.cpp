//
//  FEPrescribedActiveContractionUniaxialUC.cpp
//  FEBioMech
//
//  Created by Gerard Ateshian on 3/13/15.
//  Copyright (c) 2015 febio.org. All rights reserved.
//

#include "FEPrescribedActiveContractionUniaxialUC.h"

//-----------------------------------------------------------------------------
// define the material parameters
BEGIN_PARAMETER_LIST(FEPrescribedActiveContractionUniaxialUC, FEUncoupledMaterial)
ADD_PARAMETER(m_T0 , FE_PARAM_DOUBLE, "T0"   );
ADD_PARAMETER(m_thd, FE_PARAM_DOUBLE, "theta");
ADD_PARAMETER(m_phd, FE_PARAM_DOUBLE, "phi"  );
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
FEPrescribedActiveContractionUniaxialUC::FEPrescribedActiveContractionUniaxialUC(FEModel* pfem) : FEUncoupledMaterial(pfem)
{
    m_thd = 0;
    m_phd = 90;
}

//-----------------------------------------------------------------------------
void FEPrescribedActiveContractionUniaxialUC::Init()
{
    // convert angles from degrees to radians
    double pi = 4*atan(1.0);
    double the = m_thd*pi/180.;
    double phi = m_phd*pi/180.;
    // fiber direction in local coordinate system (reference configuration)
    m_n0.x = cos(the)*sin(phi);
    m_n0.y = sin(the)*sin(phi);
    m_n0.z = cos(phi);
}

//-----------------------------------------------------------------------------
// Since the prescribed active contraction stress is not dependent on a strain
// energy density function, we don't return the deviatoric part of the
// stress.  Instead, we return the actual stress.
mat3ds FEPrescribedActiveContractionUniaxialUC::DevStress(FEMaterialPoint &mp)
{
    FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
    
    // deformation gradient
    mat3d &F = pt.m_F;
    
    // get the initial fiber direction
    vec3d n0, nt;
    
    // evaluate fiber direction in global coordinate system
    n0 = pt.m_Q*m_n0;
    
    // evaluate the deformed fiber direction
    nt = F*n0;
    nt.unit();
    mat3ds N = dyad(nt);
    
    // evaluate the active stress
    mat3ds s = N*m_T0;
    
    return s;
}

//-----------------------------------------------------------------------------
// Since the prescribed active contraction stress is not dependent on a strain
// energy density function, we don't return the deviatoric part of the
// tangent.  Instead, we return the actual tangent.
tens4ds FEPrescribedActiveContractionUniaxialUC::DevTangent(FEMaterialPoint &mp)
{
    FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
    
    // deformation gradient
    mat3d &F = pt.m_F;
    
    // get the initial fiber direction
    vec3d n0, nt;
    
    // evaluate fiber direction in global coordinate system
    n0 = pt.m_Q*m_n0;
    
    // evaluate the deformed fiber direction
    nt = F*n0;
    nt.unit();
    mat3ds N = dyad(nt);
    mat3dd I(1);
    
    tens4ds c = (dyad1s(I, N)/2.0 - dyad1s(N)*2)*m_T0;
    
    return c;
}
