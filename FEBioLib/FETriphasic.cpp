// FETriphasic.cpp: implementation of the FETriphasic class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "FETriphasic.h"
#include "FECore/FEModel.h"

#ifndef SQR
#define SQR(x) ((x)*(x))
#endif

// Material parameters for the FETriphasic material
BEGIN_PARAMETER_LIST(FETriphasic, FEMaterial)
ADD_PARAMETER(m_phi0, FE_PARAM_DOUBLE, "phi0");
ADD_PARAMETER(m_rhoTw, FE_PARAM_DOUBLE, "fluid_density");
ADD_PARAMETER(m_cFr, FE_PARAM_DOUBLE, "fixed_charge_density");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
//! FETriphasic constructor

FETriphasic::FETriphasic()
{	
	m_cFr = 0;
	m_Rgas = 0; m_Tabs = 0; m_Fc = 0;
	m_phi0 = 0;
	m_rhoTw = 0;

	AddComponent<FEElasticMaterial      >(&m_pSolid    , "solid"              );
	AddComponent<FEHydraulicPermeability>(&m_pPerm     , "permeability"       );
	AddComponent<FEOsmoticCoefficient   >(&m_pOsmC     , "osmotic_coefficient");
	AddComponent<FESolute               >(&m_pSolute[0], "solute",           0);
	AddComponent<FESolute               >(&m_pSolute[1], "solute",           1);
}

//-----------------------------------------------------------------------------
void FETriphasic::Init()
{
	FEMaterial::Init();
	m_pSolid->Init();
	m_pPerm->Init();
	m_pOsmC->Init();

	if (m_pSolute.size() != 2) throw MaterialError("Exactly two solutes must be specified");
	m_pSolute[0]->Init(); m_pSolute[1]->Init();
	
	if (!INRANGE(m_phi0, 0.0, 1.0)) throw MaterialError("phi0 must be in the range 0 <= phi0 <= 1");
	if (m_rhoTw < 0) throw MaterialError("fluid_density must be positive");
	if ((m_pSolute[0]->ChargeNumber() != 1) && (m_pSolute[0]->ChargeNumber() != -1))
		throw MaterialError("charge_number for first solute must be +1 or -1");
	if ((m_pSolute[1]->ChargeNumber() != 1) && (m_pSolute[1]->ChargeNumber() != -1))
		throw MaterialError("charge_number for second solute must be +1 or -1");
	if (m_pSolute[0]->ChargeNumber() != -m_pSolute[1]->ChargeNumber())
		throw MaterialError("charge_number of solutes must have opposite signs");
	
	m_Rgas = FEModel::GetGlobalConstant("R");
	m_Tabs = FEModel::GetGlobalConstant("T");
	m_Fc = FEModel::GetGlobalConstant("Fc");
	
	if (m_Rgas <= 0) throw MaterialError("A positive universal gas constant R must be defined in Globals section");
	if (m_Tabs <= 0) throw MaterialError("A positive absolute temperature T must be defined in Globals section");
	if (m_Fc <= 0) throw MaterialError("A positive Faraday constant Fc must be defined in Globals section");
	
}

//-----------------------------------------------------------------------------
//! Porosity in current configuration
double FETriphasic::Porosity(FEMaterialPoint& pt)
{
	FEElasticMaterialPoint& et = *pt.ExtractData<FEElasticMaterialPoint>();
	FEBiphasicMaterialPoint& pet = *pt.ExtractData<FEBiphasicMaterialPoint>();
	
	// relative volume
	double J = et.J;
	// porosity
	//	double phiw = 1 - m_phi0/J;
	double phi0 = pet.m_phi0;
	double phiw = 1 - phi0/J;
	// check for pore collapse
	// TODO: throw an error if pores collapse
	phiw = (phiw > 0) ? phiw : 0;
	
	return phiw;
}

//-----------------------------------------------------------------------------
//! Fixed charge density in current configuration
double FETriphasic::FixedChargeDensity(FEMaterialPoint& pt)
{
	FEElasticMaterialPoint& et = *pt.ExtractData<FEElasticMaterialPoint>();
	FEBiphasicMaterialPoint& pet = *pt.ExtractData<FEBiphasicMaterialPoint>();
	
	// relative volume
	double J = et.J;
	double phi0 = pet.m_phi0;
	double cF = m_cFr*(1-phi0)/(J-phi0);
	
	return cF;
}

//-----------------------------------------------------------------------------
//! Electric potential
double FETriphasic::ElectricPotential(FEMaterialPoint& pt, const bool eform)
{
	int i, j;
	
	// Solve electroneutrality polynomial for zeta
	FESaltMaterialPoint& set = *pt.ExtractData<FESaltMaterialPoint>();
	const int nsol = 2;
	double cF = FixedChargeDensity(pt);
	double c[2];		// effective concentration
	double khat[2];		// solubility
	int z[2];			// charge number
	for (i=0; i<nsol; ++i) {
		c[i] = set.m_c[i];
		khat[i] = m_pSolute[i]->m_pSolub->Solubility(pt);
		z[i] = m_pSolute[i]->ChargeNumber();
	}
	double zeta, psi;
	
	// evaluate polynomial coefficients
	double a[3] = {0};
	for (i=0; i<nsol; ++i) {
		j = z[i] + 1;
		a[j] += z[i]*khat[i]*c[i];
	}
	a[1] = cF;
	
	// solve polynomial
	zeta = 1.0;
	if (a[2]) {
		zeta = (-a[1]+sqrt(a[1]*a[1]-4*a[0]*a[2]))/(2*a[2]);	// quadratic
	} else if (a[1]) {
		zeta = -a[0]/a[1];			// linear
	}
	
	// Return exponential (non-dimensional) form if desired
	if (eform) return zeta;
	
	// Otherwise return dimensional value of electric potential
	psi = -m_Rgas*m_Tabs/m_Fc*log(zeta);
	
	return psi;
}

//-----------------------------------------------------------------------------
//! actual concentration
double FETriphasic::Concentration(FEMaterialPoint& pt, const int ion)
{
	FESaltMaterialPoint& spt = *pt.ExtractData<FESaltMaterialPoint>();
	
	// effective concentration
	double c = spt.m_c[ion];
	// solubility
	double khat = m_pSolute[ion]->m_pSolub->Solubility(pt);
	int z = m_pSolute[ion]->ChargeNumber();
	double zeta = ElectricPotential(pt, true);
	double zz = pow(zeta, z);
	double kappa = zz*khat;
	
	// actual concentration
	double ca = kappa*c;
	
	return ca;
}

//-----------------------------------------------------------------------------
//! The stress of a triphasic material is the sum of the fluid pressure
//! and the elastic stress. Note that this function is declared in the base class
//! so you do not have to reimplement it in a derived class, unless additional
//! pressure terms are required.

mat3ds FETriphasic::Stress(FEMaterialPoint& mp)
{
	FEBiphasicMaterialPoint& pt = *mp.ExtractData<FEBiphasicMaterialPoint>();
	
	// calculate solid material stress
	mat3ds s = m_pSolid->Stress(mp);
	
	// fluid pressure
	double p = Pressure(mp);
	
	// add fluid pressure
	s.xx() -= p;
	s.yy() -= p;
	s.zz() -= p;
	
	return s;
}

//-----------------------------------------------------------------------------
//! The tangent is the elastic tangent. Note
//! that this function is declared in the base class, so you don't have to 
//! reimplement it unless additional tangent components are required.

tens4ds FETriphasic::Tangent(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& ept = *mp.ExtractData<FEElasticMaterialPoint>();
	FEBiphasicMaterialPoint& ppt = *mp.ExtractData<FEBiphasicMaterialPoint>();
	FESaltMaterialPoint& spt = *mp.ExtractData<FESaltMaterialPoint>();
	
	// call solid tangent routine
	tens4ds C = m_pSolid->Tangent(mp);
	
	// relative volume and solid volume fraction
	double J = ept.J;
	double phi0 = ppt.m_phi0;
	
	// get the charge density and its derivatives
	double cF = FixedChargeDensity(mp);
	double dcFdJ = -cF/(J - phi0);
	
	// fluid pressure and solute concentration
	double p = Pressure(mp);
	
	// get the effective concentration
	double c[2] = {spt.m_c[0],spt.m_c[1]};
	
	// get the charge number
	int z[2] = {m_pSolute[0]->ChargeNumber(),m_pSolute[1]->ChargeNumber()};
	
	// evaluate the solubility and its derivatives w.r.t. J and c
	double khat[2] = {
		m_pSolute[0]->m_pSolub->Solubility(mp),
		m_pSolute[1]->m_pSolub->Solubility(mp)};
	double dkhdJ[2] = {
		m_pSolute[0]->m_pSolub->Tangent_Solubility_Strain(mp),
		m_pSolute[1]->m_pSolub->Tangent_Solubility_Strain(mp)};
	
	// evaluate electric potential (nondimensional exponential form) and its derivatives
	// also evaluate partition coefficients and their derivatives
	double zeta = ElectricPotential(mp, true);
	double zz[2] = {pow(zeta, z[0]), pow(zeta, z[1])};
	double kappa[2] = {zz[0]*khat[0], zz[1]*khat[1]};
	double den = SQR(z[0])*kappa[0]*c[0]+SQR(z[1])*kappa[1]*c[1];
	double zidzdJ = 0;
	if (den > 0) zidzdJ = -(dcFdJ+z[0]*zz[0]*dkhdJ[0]*c[0]
							+z[1]*zz[1]*dkhdJ[1]*c[1])/den;
	double dkdJ[2] = {
		zz[0]*dkhdJ[0]+z[0]*kappa[0]*zidzdJ,
		zz[1]*dkhdJ[1]+z[1]*kappa[1]*zidzdJ};
	
	// osmotic coefficient and its derivative w.r.t. strain
	double osmc = m_pOsmC->OsmoticCoefficient(mp);
	double dodJ = m_pOsmC->Tangent_OsmoticCoefficient_Strain(mp);
	
	double dp = m_Rgas*m_Tabs*J*(c[0]*(osmc*dkdJ[0]+dodJ*kappa[0]) +c[1]*(osmc*dkdJ[1]+dodJ*kappa[1]));
	
	// adjust tangent for pressures
	double D[6][6] = {0};
	C.extract(D);
	
	D[0][0] -= -p + dp;
	D[1][1] -= -p + dp;
	D[2][2] -= -p + dp;
	
	D[0][1] -= p + dp; D[1][0] -= p + dp;
	D[1][2] -= p + dp; D[2][1] -= p + dp;
	D[0][2] -= p + dp; D[2][0] -= p + dp;
	
	D[3][3] -= -p;
	D[4][4] -= -p;
	D[5][5] -= -p;
	
	return tens4ds(D);
}

//-----------------------------------------------------------------------------
//! Calculate fluid flux

vec3d FETriphasic::FluidFlux(FEMaterialPoint& pt)
{
	FEBiphasicMaterialPoint& ppt = *pt.ExtractData<FEBiphasicMaterialPoint>();
	FESaltMaterialPoint& spt = *pt.ExtractData<FESaltMaterialPoint>();
	
	// fluid volume fraction (porosity) in current configuration
	double phiw = Porosity(pt);
	
	// pressure gradient
	vec3d gradp = ppt.m_gradp;
	
	// concentration
	double c[2] = {spt.m_c[0], spt.m_c[1]};
	
	// concentration gradient
	vec3d gradc[2] = {spt.m_gradc[0], spt.m_gradc[1]};
	
	// solute diffusivity in mixture
	mat3ds D[2] = {
		m_pSolute[0]->m_pDiff->Diffusivity(pt),
		m_pSolute[1]->m_pDiff->Diffusivity(pt)};
	
	// solute free diffusivity
	double D0[2] = {
		m_pSolute[0]->m_pDiff->Free_Diffusivity(pt),
		m_pSolute[1]->m_pDiff->Free_Diffusivity(pt)};
	
	// solubility
	double khat[2] = {
		m_pSolute[0]->m_pSolub->Solubility(pt),
		m_pSolute[1]->m_pSolub->Solubility(pt)};
	int z[2] = {
		m_pSolute[0]->ChargeNumber(),
		m_pSolute[1]->ChargeNumber()};
	double zeta = ElectricPotential(pt, true);
	double zz[2] = {pow(zeta, z[0]),pow(zeta, z[1])};
	double kappa[2] = {zz[0]*khat[0],zz[1]*khat[1]};
	
	// identity matrix
	mat3dd I(1);
	
	// hydraulic permeability
	mat3ds kt = m_pPerm->Permeability(pt);
	
	// effective hydraulic permeability
	mat3ds ke = kt.inverse() +
	( (I-D[0]/D0[0])*(kappa[0]*c[0]/D0[0])
	 +(I-D[1]/D0[1])*(kappa[1]*c[1]/D0[1])
	 )*(m_Rgas*m_Tabs/phiw);
	ke = ke.inverse();
	
	// fluid flux w
	vec3d w = -(ke*(gradp + ((D[0]*gradc[0])*(kappa[0]/D0[0])
					+ (D[1]*gradc[1])*(kappa[1]/D0[1]))*m_Rgas*m_Tabs));
	
	return w;
}

//-----------------------------------------------------------------------------
//! Calculate solute molar flux

vec3d FETriphasic::SoluteFlux(FEMaterialPoint& pt, const int ion)
{
	FEBiphasicMaterialPoint& ppt = *pt.ExtractData<FEBiphasicMaterialPoint>();
	FESaltMaterialPoint& spt = *pt.ExtractData<FESaltMaterialPoint>();
	
	// fluid volume fraction (porosity) in current configuration
	double phiw = Porosity(pt);
	
	// pressure gradient
	vec3d gradp = ppt.m_gradp;
	
	// concentration
	double c = spt.m_c[ion];
	
	// concentration gradient
	vec3d gradc = spt.m_gradc[ion];
	
	// solute diffusivity in mixture
	mat3ds D = m_pSolute[ion]->m_pDiff->Diffusivity(pt);
	
	// solute free diffusivity
	double D0 = m_pSolute[ion]->m_pDiff->Free_Diffusivity(pt);
	
	// solubility
	double khat = m_pSolute[ion]->m_pSolub->Solubility(pt);
	int z = m_pSolute[ion]->ChargeNumber();
	double zeta = ElectricPotential(pt, true);
	double zz = pow(zeta, z);
	double kappa = zz*khat;

	// fluid flux w
	vec3d w = FluidFlux(pt);
	
	// solute flux j
	vec3d j = D*(w*(c/D0) - gradc*phiw)*kappa;
	
	return j;
}

//-----------------------------------------------------------------------------
//! actual fluid pressure
double FETriphasic::Pressure(FEMaterialPoint& pt)
{
	FEBiphasicMaterialPoint& ppt = *pt.ExtractData<FEBiphasicMaterialPoint>();
	FESaltMaterialPoint& spt = *pt.ExtractData<FESaltMaterialPoint>();
	
	// effective pressure
	double p = ppt.m_p;
	
	// effective concentration
	double ca[2] = {Concentration(pt, 0), Concentration(pt, 1)};
	
	// osmotic coefficient
	double osmc = m_pOsmC->OsmoticCoefficient(pt);
	
	// actual pressure
	double pa = p + m_Rgas*m_Tabs*osmc*(ca[0]+ca[1]);
	
	return pa;
}

//-----------------------------------------------------------------------------
//! Current density
vec3d FETriphasic::CurrentDensity(FEMaterialPoint& pt)
{
	vec3d j[2];
	j[0] = SoluteFlux(pt, 0);
	j[1] = SoluteFlux(pt, 1);
	int z[2] = {m_pSolute[0]->ChargeNumber(), m_pSolute[1]->ChargeNumber()};
	
	vec3d Ie = (j[0]*z[0] + j[1]*z[1])*m_Fc;
	
	return Ie;
}

//-----------------------------------------------------------------------------
//! Data serialization
void FETriphasic::Serialize(DumpFile& ar)
{
	FEMaterial::Serialize(ar);
	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	if (ar.IsSaving())
	{
		ar << m_phi0 << m_rhoTw << m_cFr << m_Rgas << m_Tabs << m_Fc;
		
		ar << febio.GetTypeStr<FEMaterial>(m_pSolid    ); m_pSolid->Serialize(ar);
		ar << febio.GetTypeStr<FEMaterial>(m_pPerm     ); m_pPerm ->Serialize(ar);
		ar << febio.GetTypeStr<FEMaterial>(m_pSolute[0]); m_pSolute[0] ->Serialize(ar);
		ar << febio.GetTypeStr<FEMaterial>(m_pSolute[1]); m_pSolute[1] ->Serialize(ar);
		ar << febio.GetTypeStr<FEMaterial>(m_pOsmC     ); m_pOsmC ->Serialize(ar);
	}
	else
	{
		ar >> m_phi0 >> m_rhoTw >> m_cFr >> m_Rgas >> m_Tabs >> m_Fc;
		
		char sz[256] = {0};
		ar >> sz;
		m_pSolid = dynamic_cast<FEElasticMaterial*>(febio.Create<FEMaterial>(sz, ar.GetFEModel()));
		assert(m_pSolid); m_pSolid->Serialize(ar);
		m_pSolid->Init();
		
		ar >> sz;
		m_pPerm = dynamic_cast<FEHydraulicPermeability*>(febio.Create<FEMaterial>(sz, ar.GetFEModel()));
		assert(m_pPerm); m_pPerm->Serialize(ar);
		m_pPerm->Init();

		ar >> sz;
		m_pOsmC = dynamic_cast<FEOsmoticCoefficient*>(febio.Create<FEMaterial>(sz, ar.GetFEModel()));
		assert(m_pOsmC); m_pOsmC->Serialize(ar);
		m_pOsmC->Init();
		
		ar >> sz;
		m_pSolute[0] = dynamic_cast<FESolute*>(febio.Create<FEMaterial>(sz, ar.GetFEModel()));
		assert(m_pSolute[0]); m_pSolute[0]->Serialize(ar);
		m_pSolute[0]->Init();
		
		ar >> sz;
		m_pSolute[1] = dynamic_cast<FESolute*>(febio.Create<FEMaterial>(sz, ar.GetFEModel()));
		assert(m_pSolute[1]); m_pSolute[1]->Serialize(ar);
		m_pSolute[1]->Init();
	}
}
