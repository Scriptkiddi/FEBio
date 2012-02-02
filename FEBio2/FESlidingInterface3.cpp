#include "stdafx.h"
#include "fem.h"
#include "FESlidingInterface3.h"
#include "FEBioLib/FEBiphasic.h"
#include "FEBioLib/FEBiphasicSolute.h"
#include "FEBioLib/log.h"

//-----------------------------------------------------------------------------
// Register the class with the framework
REGISTER_FEBIO_CLASS(FESlidingInterface3, FEContactInterface, "sliding3");

//-----------------------------------------------------------------------------
// Define sliding interface parameters
BEGIN_PARAMETER_LIST(FESlidingInterface3, FEContactInterface)
	ADD_PARAMETER(m_blaugon  , FE_PARAM_BOOL  , "laugon"               ); 
	ADD_PARAMETER(m_atol     , FE_PARAM_DOUBLE, "tolerance"            );
	ADD_PARAMETER(m_gtol     , FE_PARAM_DOUBLE, "gaptol"               );
	ADD_PARAMETER(m_ptol     , FE_PARAM_DOUBLE, "ptol"                 );
	ADD_PARAMETER(m_ctol     , FE_PARAM_DOUBLE, "ctol"                 );
	ADD_PARAMETER(m_epsn     , FE_PARAM_DOUBLE, "penalty"              );
	ADD_PARAMETER(m_bautopen , FE_PARAM_BOOL  , "auto_penalty"         );
	ADD_PARAMETER(m_btwo_pass, FE_PARAM_BOOL  , "two_pass"             );
	ADD_PARAMETER(m_knmult   , FE_PARAM_DOUBLE, "knmult"               );
	ADD_PARAMETER(m_stol     , FE_PARAM_DOUBLE, "search_tol"           );
	ADD_PARAMETER(m_epsp     , FE_PARAM_DOUBLE, "pressure_penalty"     );
	ADD_PARAMETER(m_epsc     , FE_PARAM_DOUBLE, "concentration_penalty");
	ADD_PARAMETER(m_bsymm    , FE_PARAM_BOOL  , "symmetric_stiffness"  );
	ADD_PARAMETER(m_srad     , FE_PARAM_DOUBLE, "search_radius"        );
	ADD_PARAMETER(m_nsegup   , FE_PARAM_INT   , "seg_up"               );
	ADD_PARAMETER(m_ambp     , FE_PARAM_DOUBLE, "ambient_pressure"     );
	ADD_PARAMETER(m_ambc     , FE_PARAM_DOUBLE, "ambient_concentration");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
// FESlidingSurface3
//-----------------------------------------------------------------------------

FESlidingSurface3::FESlidingSurface3(FEModel* pfem) : FEContactSurface(&pfem->m_mesh)
{ 
	m_bporo = m_bsolu = false;
	m_pfem = pfem; 
}

//-----------------------------------------------------------------------------
void FESlidingSurface3::Init()
{
	// initialize surface data first
	FEContactSurface::Init();
	
	// count how many integration points we have
	int nint = 0, i;
	for (i=0; i<Elements(); ++i)
	{
		FESurfaceElement& el = Element(i);
		nint += el.GaussPoints();
	}
	
	// allocate data structures
	m_gap.assign(nint, 0.0);
	m_nu.resize(nint);
	m_rs.resize(nint);
	m_Lmd.assign(nint, 0.0);
	m_Lmp.assign(nint, 0.0);
	m_Lmc.assign(nint, 0.0);
	m_pme.assign(nint, static_cast<FESurfaceElement*>(0));
	m_epsn.assign(nint, 1.0);
	m_epsp.assign(nint, 1.0);
	m_epsc.assign(nint, 1.0);
	m_Ln.assign(nint, 0.0);
	
	m_nn.assign(Nodes(), 0);
	
	m_nei.resize(Elements());
	nint = 0;
	for (i=0; i<Elements(); ++i)
	{
		m_nei[i] = nint;
		nint += Element(i).GaussPoints();
	}
	
	// set intial values
	zero(m_nu);
	
	// determine biphasic status
	i = 0;
	while (!m_bporo && (i<Elements())) {
		// get the surface element
		FESurfaceElement& se = Element(i);

		// get the solid element this surface element belongs to
		FESolidElement* pe = dynamic_cast<FESolidElement*>(m_pMesh->FindElementFromID(se.m_nelem));
		if (pe)
		{
			// get the material
			FEMaterial* pm = dynamic_cast<FEMaterial*>(m_pfem->GetMaterial(pe->GetMatID()));
			
			// see if this is a poro-elastic element
			FEBiphasic* biph = dynamic_cast<FEBiphasic*> (pm);
			FEBiphasicSolute* bsolu = dynamic_cast<FEBiphasicSolute*> (pm);
			if (biph || bsolu) m_bporo = true;
		}
		++i;
	}
	
	// determine solute status
	i = 0;
	while (!m_bsolu && (i<Elements())) {
		// get the surface element
		FESurfaceElement& se = Element(i);

		// get the solid element this surface element belongs to
		FESolidElement* pe = dynamic_cast<FESolidElement*>(m_pMesh->FindElementFromID(se.m_nelem));
		if (pe)
		{
			// get the material
			FEMaterial* pm = dynamic_cast<FEMaterial*>(m_pfem->GetMaterial(pe->GetMatID()));
			
			// see if this is a biphasic-solute element
			FEBiphasicSolute* bsolu = dynamic_cast<FEBiphasicSolute*> (pm);
			if (bsolu) m_bsolu = true;
		}
		++i;
	}
	
	// allocate biphasic stuff
	if (m_bporo)
	{
		m_pg.assign(nint, 0);
	}
	if (m_bsolu)
	{
		m_cg.assign(nint, 0);
	}
}

//-----------------------------------------------------------------------------
void FESlidingSurface3::ShallowCopy(FESlidingSurface3 &s)
{
	m_Lmd = s.m_Lmd;
	m_gap = s.m_gap;
	m_Ln = s.m_Ln;
	zero(m_pme);
	m_bporo = s.m_bporo;
	m_bsolu = s.m_bsolu;
	
	if (m_bporo)
	{
		m_pg  = s.m_pg;
		m_Lmp = s.m_Lmp;
	}
	if (m_bsolu)
	{
		m_cg  = s.m_cg;
		m_Lmc = s.m_Lmc;
	}
}

//-----------------------------------------------------------------------------
//! This function calculates the node normal. Due to the piecewise continuity
//! of the surface elements this normal is not uniquely defined so in order to
//! obtain a unique normal the normal is averaged for each node over all the 
//! element normals at the node

void FESlidingSurface3::UpdateNodeNormals()
{
	int N = Nodes(), i, j, ne, jp1, jm1;
	vec3d y[4], n;
	
	// zero nodal normals
	zero(m_nn);
	
	// loop over all elements
	for (i=0; i<Elements(); ++i)
	{
		FESurfaceElement& el = Element(i);
		ne = el.Nodes();
		
		// get the nodal coordinates
		for (j=0; j<ne; ++j) y[j] = Node(el.m_lnode[j]).m_rt;
		
		// calculate the normals
		for (j=0; j<ne; ++j)
		{
			jp1 = (j+1)%ne;
			jm1 = (j+ne-1)%ne;
			n = (y[jp1] - y[j]) ^ (y[jm1] - y[j]);
			m_nn[el.m_lnode[j]] += n;
		}
	}
	
	// normalize all vectors
	for (i=0; i<N; ++i) m_nn[i].unit();
}

//-----------------------------------------------------------------------------
void FESlidingSurface3::Serialize(DumpFile& ar)
{
	// We need to store the m_bporo and m_bsolu flags first 
	// since we need them before we initialize the surface data
	if (ar.IsSaving())
	{
		ar << m_bporo;
		ar << m_bsolu;
	}
	else
	{
		ar >> m_bporo;
		ar >> m_bsolu;
	}
	
	// Next, we can serialize the base-class data
	FEContactSurface::Serialize(ar);
	
	// And finally, we serialize the surface data
	if (ar.IsSaving())
	{
		ar << m_gap;
		ar << m_nu;
		ar << m_rs;
		ar << m_Lmd;
		ar << m_Lmp;
		ar << m_Lmc;
		ar << m_nei;
		ar << m_epsn;
		ar << m_epsp;
		ar << m_epsc;
		ar << m_nn;
		ar << m_pg;
		ar << m_cg;
		ar << m_Ln;
		
		int ne = (int) m_pme.size();
		ar << ne;
		for (int i=0; i<ne; ++i)
		{
			FESurfaceElement* pe = m_pme[i];
			if (pe) ar << pe->m_lid; else ar << -1;
		}
	}
	else
	{
		ar >> m_gap;
		ar >> m_nu;
		ar >> m_rs;
		ar >> m_Lmd;
		ar >> m_Lmp;
		ar >> m_Lmc;
		ar >> m_nei;
		ar >> m_epsn;
		ar >> m_epsp;
		ar >> m_epsc;
		ar >> m_nn;
		ar >> m_pg;
		ar >> m_cg;
		ar >> m_Ln;
		
		assert(m_pSibling);
		
		int ne = 0, id;
		ar >> ne;
		assert(ne == m_pme.size());
		for (int i=0; i<ne; ++i)
		{
			ar >> id;
			if (id < 0) m_pme[i] = 0; 
			else 
			{
				m_pme[i] = &m_pSibling->Element(id);
				assert(m_pme[i]->m_lid == id);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// FESlidingInterface3
//-----------------------------------------------------------------------------

FESlidingInterface3::FESlidingInterface3(FEModel* pfem) : FEContactInterface(pfem), m_ss(pfem), m_ms(pfem)
{
	m_ntype = FE_CONTACT_SLIDING3;
	static int count = 1;
	m_nID = count++;
	
	// initial values
	m_knmult = 1;
	m_atol = 0.1;
	m_epsn = 1;
	m_epsp = 1;
	m_epsc = 1;
	m_btwo_pass = false;
	m_stol = 0.01;
	m_bsymm = true;
	m_srad = 0.1;
	m_gtol = -1;	// we use augmentation tolerance by default
	m_ptol = -1;	// we use augmentation tolerance by default
	m_ctol = -1;	// we use augmentation tolerance by default
	m_ambp = 0;
	m_ambc = 0;
	m_bautopen = false;
	
	m_naugmin = 0;
	m_naugmax = 10;
	
	m_ss.SetSibling(&m_ms);
	m_ms.SetSibling(&m_ss);
}

//-----------------------------------------------------------------------------

FESlidingInterface3::~FESlidingInterface3()
{
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::Init()
{
	m_Rgas = FEModel::GetGlobalConstant("R");
	m_Tabs = FEModel::GetGlobalConstant("T");

	// initialize surface data
	m_ss.Init();
	m_ms.Init();
	
	bool bporo = (m_ss.m_bporo || m_ms.m_bporo);

	FEM& fem = dynamic_cast<FEM&>(*m_pfem);
	
	// this contact implementation requires a non-symmetric stiffness matrix
	// so inform the FEM class
	if (!m_bsymm) 
	{
		// request a non-symmetric stiffness matrix
		fem.SetSymmetryFlag(false);
		
		// make sure we are using full-Newton
		FEAnalysis* pstep = fem.GetCurrentStep();
		if (bporo && (pstep->m_psolver->m_bfgs.m_maxups != 0))
		{
			pstep->m_psolver->m_bfgs.m_maxups = 0;
			clog.printbox("WARNING", "The non-symmetric biphasic-solute contact algorithm does not work with BFGS yet.\nThe full-Newton method will be used instead.");
		}
	}
	
	// calculate the penalty
	if (m_bautopen) 
	{
		CalcAutoPenalty(m_ss);
		CalcAutoPenalty(m_ms);
		if (m_ss.m_bporo) CalcAutoPressurePenalty(m_ss);
		if (m_ss.m_bsolu) CalcAutoConcentrationPenalty(m_ss);
		if (m_ms.m_bporo) CalcAutoPressurePenalty(m_ms);
		if (m_ms.m_bsolu) CalcAutoConcentrationPenalty(m_ms);
	}
	
	// update sliding interface data
	Update(0);
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::CalcAutoPenalty(FESlidingSurface3& s)
{
	// get the mesh
	FEMesh& m = m_pfem->m_mesh;
	
	// loop over all surface elements
	int ni = 0;
	for (int i=0; i<s.Elements(); ++i)
	{
		// get the surface element
		FESurfaceElement& el = s.Element(i);
		
		// find the element this face belongs to
		FEElement* pe = m.FindElementFromID(el.m_nelem);
		assert(pe);
		
		// get the area of the surface element
		double A = s.FaceArea(el);
		
		// get the volume of the volume element
		double V = m.ElementVolume(*pe);
		
		// calculate a modulus
		double K = BulkModulus(el, s);
		
		// calculate penalty
		double eps = K*A/V;
		
		// assign to integation points of surface element
		int nint = el.GaussPoints();
		for (int j=0; j<nint; ++j, ++ni) s.m_epsn[ni] = eps;
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::CalcAutoPressurePenalty(FESlidingSurface3& s)
{
	// get the mesh
	FEMesh& m = m_pfem->m_mesh;
	
	// loop over all surface elements
	int ni = 0;
	for (int i=0; i<s.Elements(); ++i)
	{
		// get the surface element
		FESurfaceElement& el = s.Element(i);
		
		// find the element this face belongs to
		FEElement* pe = m.FindElementFromID(el.m_nelem);
		assert(pe);
		
		// get the area of the surface element
		double A = s.FaceArea(el);
		
		// get the volume of the volume element
		double V = m.ElementVolume(*pe);
		
		// calculate a modulus
		double k = AutoPressurePenalty(el, s);
		
		// calculate penalty
		double eps = k*A/V;
		
		// assign to integation points of surface element
		int nint = el.GaussPoints();
		for (int j=0; j<nint; ++j, ++ni) s.m_epsp[ni] = eps;
	}
}

//-----------------------------------------------------------------------------

double FESlidingInterface3::AutoPressurePenalty(FESurfaceElement& el, FESlidingSurface3& s)
{
	// get the mesh
	FEMesh& m = m_pfem->m_mesh;
	
	double eps = 0;
	
	// get the solid element this surface element belongs to
	FESolidElement* pe = dynamic_cast<FESolidElement*>(m.FindElementFromID(el.m_nelem));
	if (pe)
	{
		// get the material
		FEMaterial* pm = dynamic_cast<FEMaterial*>(m_pfem->GetMaterial(pe->GetMatID()));
		
		// see if this is a poro-elastic element
		FEBiphasic* biph = dynamic_cast<FEBiphasic*> (pm);
		FEBiphasicSolute* bsolu = dynamic_cast<FEBiphasicSolute*> (pm);
		if (biph)
		{
			// get a material point
			FEMaterialPoint& mp = *pe->m_State[0];
			FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
			
			// setup the material point
			ept.F = mat3dd(1.0);
			ept.J = 1;
			ept.s.zero();
			
			// if this is a poroelastic element, then get the permeability tensor
			FEBiphasicMaterialPoint& pt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
			pt.m_p = 0;
			pt.m_w = vec3d(0,0,0);
			
			double K[3][3];
			biph->Permeability(K, mp);
			
			eps = (K[0][0] + K[1][1] + K[2][2])/3;
		}
		else if (bsolu)
		{
			// get a material point
			FEMaterialPoint& mp = *pe->m_State[0];
			FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
			
			// setup the material point
			ept.F = mat3dd(1.0);
			ept.J = 1;
			ept.s.zero();
			
			// if this is a biphasic-solute element, then get the permeability tensor
			FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
			FESoluteMaterialPoint& spt = *(mp.ExtractData<FESoluteMaterialPoint>());
			ppt.m_p = 0;
			ppt.m_w = vec3d(0,0,0);
			spt.m_c = 0;
			spt.m_j = vec3d(0,0,0);
			
			mat3ds K = bsolu->m_pPerm->Permeability(mp);
			
			eps = K.tr()/3;
		}
	}
	
	return eps;
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::CalcAutoConcentrationPenalty(FESlidingSurface3& s)
{
	// get the mesh
	FEMesh& m = m_pfem->m_mesh;
	
	// loop over all surface elements
	int ni = 0;
	for (int i=0; i<s.Elements(); ++i)
	{
		// get the surface element
		FESurfaceElement& el = s.Element(i);
		
		// find the element this face belongs to
		FEElement* pe = m.FindElementFromID(el.m_nelem);
		assert(pe);
		
		// get the area of the surface element
		double A = s.FaceArea(el);
		
		// get the volume of the volume element
		double V = m.ElementVolume(*pe);
		
		// calculate a modulus
		double d = AutoConcentrationPenalty(el, s);
		
		// calculate penalty
		double eps = d*A/V;
		
		// assign to integation points of surface element
		int nint = el.GaussPoints();
		for (int j=0; j<nint; ++j, ++ni) s.m_epsc[ni] = eps;
	}
}

//-----------------------------------------------------------------------------

double FESlidingInterface3::AutoConcentrationPenalty(FESurfaceElement& el, FESlidingSurface3& s)
{
	// get the mesh
	FEMesh& m = m_pfem->m_mesh;
	
	double eps = 0;
	
	// get the solid element this surface element belongs to
	FESolidElement* pe = dynamic_cast<FESolidElement*>(m.FindElementFromID(el.m_nelem));
	if (pe)
	{
		// get the material
		FEMaterial* pm = dynamic_cast<FEMaterial*>(m_pfem->GetMaterial(pe->GetMatID()));
		
		// see if this is a biphasic-solute element
		FEBiphasicSolute* bsolu = dynamic_cast<FEBiphasicSolute*> (pm);
		if (bsolu)
		{
			// get a material point
			FEMaterialPoint& mp = *pe->m_State[0];
			FEElasticMaterialPoint& ept = *(mp.ExtractData<FEElasticMaterialPoint>());
			
			// setup the material point
			ept.F = mat3dd(1.0);
			ept.J = 1;
			ept.s.zero();
			
			// if this is a biphasic-solute element, then get the diffusivity tensor
			FEBiphasicMaterialPoint& ppt = *(mp.ExtractData<FEBiphasicMaterialPoint>());
			FESoluteMaterialPoint& spt = *(mp.ExtractData<FESoluteMaterialPoint>());
			ppt.m_p = 0;
			ppt.m_w = vec3d(0,0,0);
			spt.m_c = 0;
			spt.m_j = vec3d(0,0,0);
			
			mat3ds D = bsolu->m_pDiff->Diffusivity(mp)
			*(bsolu->Porosity(mp)*bsolu->m_pSolub->Solubility(mp));
			
			eps = D.tr()/3;
		}
	}
	
	return eps;
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::ProjectSurface(FESlidingSurface3& ss, FESlidingSurface3& ms, bool bupseg)
{
	FEMesh& mesh = m_pfem->m_mesh;
	FESurfaceElement* pme;
	vec3d r, nu;
	double rs[2];
	double Ln;
	
	double ps[4], p1;
	double cs[4], c1;
	
	double R = m_srad*mesh.GetBoundingBox().radius();
	
	// loop over all integration points
	int n = 0;
	for (int i=0; i<ss.Elements(); ++i)
	{
		FESurfaceElement& el = ss.Element(i);

		bool sporo, ssolu;
		BiphasicSoluteStatus(mesh, el, sporo, ssolu);
		
		int ne = el.Nodes();
		int nint = el.GaussPoints();
		
		// get the nodal pressures
		if (sporo)
		{
			for (int j=0; j<ne; ++j) ps[j] = mesh.Node(el.m_node[j]).m_pt;
		}
		
		// get the nodal concentrations
		if (ssolu)
		{
			for (int j=0; j<ne; ++j) cs[j] = mesh.Node(el.m_node[j]).m_ct[0];
		}
		
		for (int j=0; j<nint; ++j, ++n)
		{
			// calculate the global position of the integration point
			r = ss.Local2Global(el, j);
			
			// get the pressure at the integration point
			if (sporo) p1 = el.eval(ps, j);
			
			// get the concentration at the integration point
			if (ssolu) c1 = el.eval(cs, j);
			
			// calculate the normal at this integration point
			nu = ss.SurfaceNormal(el, j);
			
			// first see if the old intersected face is still good enough
			pme = ss.m_pme[n];
			if (pme)
			{
				double g;
				
				// see if the ray intersects this element
				if (ms.Intersect(*pme, r, nu, rs, g, m_stol))
				{
					ss.m_rs[n][0] = rs[0];
					ss.m_rs[n][1] = rs[1];
				}
				else
				{
					pme = 0;
				}
			}
			
			// find the intersection point with the master surface
			if (pme == 0 && bupseg) pme = ms.FindIntersection(r, nu, rs, m_stol);
			
			ss.m_pme[n] = pme;
			ss.m_nu[n] = nu;
			ss.m_rs[n][0] = rs[0];
			ss.m_rs[n][1] = rs[1];
			if (pme)
			{
				// the node could potentially be in contact
				// find the global location of the intersection point
				vec3d q = ms.Local2Global(*pme, rs[0], rs[1]);
				
				// calculate the gap function
				// NOTE: this has the opposite sign compared
				// to Gerard's notes.
				double g = nu*(r - q);
				
				double eps = m_epsn*ss.m_epsn[n];
				
				Ln = ss.m_Lmd[n] + eps*g;
				
				ss.m_gap[n] = (g <= R? g : 0);
				
				if ((Ln >= 0) && (g <= R))
				{
					
					// calculate the pressure gap function
					bool mporo, msolu;
					BiphasicSoluteStatus(mesh, *pme, mporo, msolu);
					if (sporo && mporo) {
						double pm[4];
						for (int k=0; k<pme->Nodes(); ++k) pm[k] = mesh.Node(pme->m_node[k]).m_pt;
						double p2 = pme->eval(pm, rs[0], rs[1]);
						ss.m_pg[n] = p1 - p2;
					}
					if (ssolu && msolu) {
						double cm[4];
						for (int k=0; k<pme->Nodes(); ++k) cm[k] = mesh.Node(pme->m_node[k]).m_ct[0];
						double c2 = pme->eval(cm, rs[0], rs[1]);
						ss.m_cg[n] = c1 - c2;
					}
				}
				else
				{
					ss.m_pme[n] = 0;
				}
			}
			else
			{
				// the node is not in contact
				ss.m_Lmd[n] = 0;
				ss.m_gap[n] = 0;
				if (sporo) {
					ss.m_Lmp[n] = 0;
					ss.m_pg[n] = 0;
				}
				if (ssolu) {
					ss.m_Lmc[n] = 0;
					ss.m_cg[n] = 0;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------

void FESlidingInterface3::Update(int niter)
{	
	int i, n, id, np;
	double rs[2];

	double R = m_srad*m_pfem->m_mesh.GetBoundingBox().radius();
	
	static int naug = 0;
	static int biter = 0;

	FEM& fem = dynamic_cast<FEM&>(*m_pfem);

	// get the iteration number
	// we need this number to see if we can do segment updates or not
	// also reset number of iterations after each augmentation
	FEAnalysis* pstep = fem.GetCurrentStep();
	if (pstep->m_psolver->m_niter == 0) {
		biter = 0;
		naug = pstep->m_psolver->m_naug;
	} else if (pstep->m_psolver->m_naug > naug) {
		biter = pstep->m_psolver->m_niter;
		naug = pstep->m_psolver->m_naug;
	}
	niter = pstep->m_psolver->m_niter - biter;
	bool bupseg = ((m_nsegup == 0)? true : (niter <= m_nsegup));
	// get the logfile
	//	Logfile& log = GetLogfile();
	//	log.printf("seg_up iteration # %d\n", niter+1);
	
	// project the surfaces onto each other
	// this will update the gap functions as well
	ProjectSurface(m_ss, m_ms, bupseg);
	if (m_btwo_pass) ProjectSurface(m_ms, m_ss, bupseg);
	
	// Update the net contact pressures
	UpdateContactPressures();

	// set poro flag
	bool bporo = (m_ss.m_bporo || m_ms.m_bporo);
	
	// only continue if we are doing a poro-elastic simulation
	if (bporo == false) return;
	
	// update node normals
	m_ss.UpdateNodeNormals();
	if (bporo) m_ms.UpdateNodeNormals();
	
	// Now that the nodes have been projected, we need to figure out
	// if we need to modify the constraints on the pressure and concentration dofs.
	// If the nodes are not in contact, they must be match ambient
	// conditions. Since all nodes have been previously marked to be
	// under ambient conditions in MarkAmbient(), we just need to reverse
	// this setting here, for nodes that are in contact.
	
	// Next, we loop over each surface, visiting the nodes
	// and finding out if that node is in contact or not
	int npass = (m_btwo_pass?2:1);
	for (np=0; np<npass; ++np)
	{
		FESlidingSurface3& ss = (np == 0? m_ss : m_ms);
		FESlidingSurface3& ms = (np == 0? m_ms : m_ss);
		
		// keep a running counter of integration points.
		int ni = 0;
		
		// loop over all elements of the primary surface
		for (n=0; n<ss.Elements(); ++n)
		{
			FESurfaceElement& el = ss.Element(n);
			int nint = el.GaussPoints();
			int neln = el.Nodes();
			
			// get the normal tractions at the integration points
			double ti[4], gap, eps;
			for (i=0; i<nint; ++i, ++ni) 
			{
				gap = ss.m_gap[ni];
				eps = m_epsn*ss.m_epsn[ni];
				ti[i] = MBRACKET(ss.m_Lmd[ni] + eps*gap);
			}
			
			// project the data to the nodes
			double tn[4];
			el.project_to_nodes(ti, tn);
			
			// see if we need to make changes to the BC's
			for (i=0; i<neln; ++i)
			{
				if (tn[i] > 0)
				{
					FENode& node = ss.Node(el.m_lnode[i]);
					id = node.m_ID[DOF_P];
					if (id < -1)
						// mark node as non-ambient (= pos ID)
						node.m_ID[DOF_P] = -id-2;

					id = node.m_ID[DOF_C];
					if (id < -1)
						// mark node as non-ambient (= pos ID)
						node.m_ID[DOF_C] = -id-2;
				}
			}
		}
		
		// loop over all nodes of the secondary surface
		// the secondary surface is trickier since we need
		// to look at the primary's surface projection
		for (n=0; n<ms.Nodes(); ++n)
		{
			// get the node
			FENode& node = ms.Node(n);
			
			// project it onto the primary surface
			int nei;
			FESurfaceElement* pse = ss.FindIntersection(node.m_rt, ms.m_nn[n], rs, m_stol, &nei);
			
			if (pse)
			{
				// we found an element, so let's see if it's even remotely close to contact
				// find the global location of the intersection point
				vec3d q = ms.Local2Global(*pse, rs[0], rs[1]);
				
				// calculate the gap function
				double g = ms.m_nn[n]*(node.m_rt - q);
				
				if (fabs(g) <= R)
				{
					// we found an element so let's calculate the nodal traction values for this element
					// get the normal tractions at the integration points
					double ti[4], gap, eps;
					int nint = pse->GaussPoints();
					int noff = ss.m_nei[nei];
					for (i=0; i<nint; ++i) 
					{
						gap = ss.m_gap[noff + i];
						eps = m_epsn*ss.m_epsn[noff + i];
						ti[i] = MBRACKET(ss.m_Lmd[noff + i] + eps*gap);
					}
					
					// project the data to the nodes
					double tn[4];
					pse->project_to_nodes(ti, tn);
					
					// now evaluate the traction at the intersection point
					double tp = pse->eval(tn, rs[0], rs[1]);
					
					// if tp > 0, mark node as non-ambient. (= pos ID)
					if (tp > 0)  {
						id = node.m_ID[DOF_P];
						if (id < -1)
							// mark as non-ambient
							node.m_ID[DOF_P] = -id-2;

						id = node.m_ID[DOF_C];
						if (id < -1)
							// mark as non-ambient
							node.m_ID[DOF_C] = -id-2;
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::ShallowCopy(FEContactInterface &ci)
{
	FESlidingInterface3& si = dynamic_cast<FESlidingInterface3&>(ci);
	m_ss.ShallowCopy(si.m_ss);
	m_ms.ShallowCopy(si.m_ms);
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::ContactForces(vector<double> &F, FENLSolver* psolver)
{
	int i, j, k;
	vector<int> sLM, mLM, LM, en;
	vector<double> fe;
	double detJ[4], w[4], *Hs, Hm[4];
	double N[40];

	FEM& fem = dynamic_cast<FEM&>(*m_pfem);

	// get the mesh
	FEMesh* pm = m_ss.GetMesh();
	
	
	double dt = fem.GetCurrentStep()->m_dt;
	
	// loop over the nr of passes
	int npass = (m_btwo_pass?2:1);
	for (int np=0; np<npass; ++np)
	{
		// get slave and master surface
		FESlidingSurface3& ss = (np == 0? m_ss : m_ms);
		FESlidingSurface3& ms = (np == 0? m_ms : m_ss);
		
		// keep a running counter of integration points
		int ni = 0;
		
		// loop over all slave elements
		for (i=0; i<ss.Elements(); ++i)
		{
			// get the surface element
			FESurfaceElement& se = ss.Element(i);

			bool sporo, ssolu;
			BiphasicSoluteStatus(*pm, se, sporo, ssolu);
			
			// get the nr of nodes and integration points
			int nseln = se.Nodes();
			int nint = se.GaussPoints();
			
			// get the element's LM vector
			ss.UnpackLM(se, sLM);
			
			// we calculate all the metrics we need before we
			// calculate the nodal forces
			for (j=0; j<nint; ++j)
			{
				// get the base vectors
				vec3d g[2];
				ss.CoBaseVectors(se, j, g);
				
				// jacobians: J = |g0xg1|
				detJ[j] = (g[0] ^ g[1]).norm();
				
				// integration weights
				w[j] = se.GaussWeights()[j];
			}
			
			// loop over all integration points
			// note that we are integrating over the current surface
			for (j=0; j<nint; ++j, ++ni)
			{
				// get the master element
				FESurfaceElement* pme = ss.m_pme[ni];
				if (pme)
				{
					// get the master element
					FESurfaceElement& me = *pme;

					bool mporo, msolu;
					BiphasicSoluteStatus(*pm, me, mporo, msolu);
					
					// get the nr of master element nodes
					int nmeln = me.Nodes();
					
					// get the element's LM vector
					ms.UnpackLM(me, mLM);
					
					// calculate degrees of freedom
					int ndof = 3*(nseln + nmeln);
					
					// build the LM vector
					LM.resize(ndof);
					for (k=0; k<nseln; ++k)
					{
						LM[3*k  ] = sLM[3*k  ];
						LM[3*k+1] = sLM[3*k+1];
						LM[3*k+2] = sLM[3*k+2];
					}
					
					for (k=0; k<nmeln; ++k)
					{
						LM[3*(k+nseln)  ] = mLM[3*k  ];
						LM[3*(k+nseln)+1] = mLM[3*k+1];
						LM[3*(k+nseln)+2] = mLM[3*k+2];
					}
					
					// build the en vector
					en.resize(nseln+nmeln);
					for (k=0; k<nseln; ++k) en[k      ] = se.m_node[k];
					for (k=0; k<nmeln; ++k) en[k+nseln] = me.m_node[k];
					
					// get slave element shape functions
					Hs = se.H(j);
					
					// get master element shape functions
					double r = ss.m_rs[ni][0];
					double s = ss.m_rs[ni][1];
					me.shape_fnc(Hm, r, s);
					
					// get normal vector
					vec3d nu = ss.m_nu[ni];
					
					// gap function
					double g = ss.m_gap[ni];
					
					// lagrange multiplier
					double Lm = ss.m_Lmd[ni];
					
					// penalty 
					double eps = m_epsn*ss.m_epsn[ni];
					
					// contact traction
					double tn = Lm + eps*g;
					tn = MBRACKET(tn);
					
					// calculate the force vector
					fe.resize(ndof);
					zero(fe);
					
					for (k=0; k<nseln; ++k)
					{
						N[3*k  ] = -Hs[k]*nu.x;
						N[3*k+1] = -Hs[k]*nu.y;
						N[3*k+2] = -Hs[k]*nu.z;
					}
					
					for (k=0; k<nmeln; ++k)
					{
						N[3*(k+nseln)  ] = Hm[k]*nu.x;
						N[3*(k+nseln)+1] = Hm[k]*nu.y;
						N[3*(k+nseln)+2] = Hm[k]*nu.z;
					}
					
					for (k=0; k<ndof; ++k) fe[k] += tn*N[k]*detJ[j]*w[j];
					
					// assemble the global residual
					psolver->AssembleResidual(en, LM, fe, F);
					
					// do the biphasic stuff
					// TODO: I should only do this when the node is actually in contact
					//       in other words, when tn > 0
					if (tn > 0)
					{
						if (sporo && mporo)
						{
							// calculate nr of pressure dofs
							int ndof = nseln + nmeln;
							
							// calculate the flow rate
							double epsp = m_epsp*ss.m_epsp[ni];
							
							double wn = ss.m_Lmp[ni] + epsp*ss.m_pg[ni];
							
							// fill the LM
							LM.resize(ndof);
							for (k=0; k<nseln; ++k) LM[k        ] = sLM[3*nseln+k];
							for (k=0; k<nmeln; ++k) LM[k + nseln] = mLM[3*nmeln+k];
							
							// fill the force array
							fe.resize(ndof);
							zero(fe);
							for (k=0; k<nseln; ++k) N[k      ] =  Hs[k];
							for (k=0; k<nmeln; ++k) N[k+nseln] = -Hm[k];
							
							for (k=0; k<ndof; ++k) fe[k] += dt*wn*N[k]*detJ[j]*w[j];
							
							// assemble residual
							psolver->AssembleResidual(en, LM, fe, F);
						}
						if (ssolu && msolu)
						{
							// calculate nr of concentration dofs
							int ndof = nseln + nmeln;
							
							// calculate the flow rate
							double epsc = m_epsc*ss.m_epsc[ni];
							
							double jn = ss.m_Lmc[ni] + epsc*ss.m_cg[ni];
							
							// fill the LM
							LM.resize(ndof);
							for (k=0; k<nseln; ++k) LM[k        ] = sLM[11*nseln+k];
							for (k=0; k<nmeln; ++k) LM[k + nseln] = mLM[11*nmeln+k];
							
							// fill the force array
							fe.resize(ndof);
							zero(fe);
							for (k=0; k<nseln; ++k) N[k      ] =  Hs[k];
							for (k=0; k<nmeln; ++k) N[k+nseln] = -Hm[k];
							
							for (k=0; k<ndof; ++k) fe[k] += dt*jn*N[k]*detJ[j]*w[j];
							
							// assemble residual
							psolver->AssembleResidual(en, LM, fe, F);
						}
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::ContactStiffness(FENLSolver* psolver)
{
	int i, j, k, l;
	vector<int> sLM, mLM, LM, en;
	double detJ[4], w[4], *Hs, Hm[4];
	double pt[4], dpr[4], dps[4];
	double ct[4], dcr[4], dcs[4];
	double N[40];
	matrix ke;

	FEM& fem = dynamic_cast<FEM&>(*m_pfem);

	// get the mesh
	FEMesh* pm = m_ss.GetMesh();
	
	// see how many reformations we've had to do so far
	int nref = psolver->m_nref;
	
	// set higher order stiffness mutliplier
	// NOTE: this algrotihm doesn't really need this
	// but I've added this functionality to compare with the other contact 
	// algorithms and to see the effect of the different stiffness contributions
	double knmult = m_knmult;
	if (m_knmult < 0)
	{
		int ni = int(-m_knmult);
		if (nref >= ni)
		{
			knmult = 1; 
			clog.printf("Higher order stiffness terms included.\n");
		}
		else knmult = 0;
	}
	
	// do single- or two-pass
	int npass = (m_btwo_pass?2:1);
	for (int np=0; np < npass; ++np)
	{
		// get the slave and master surface
		FESlidingSurface3& ss = (np == 0? m_ss : m_ms);
		FESlidingSurface3& ms = (np == 0? m_ms : m_ss);
		
		// keep a running counter of the integration points
		int ni = 0;
		
		// loop over all slave elements
		for (i=0; i<ss.Elements(); ++i)
		{
			// get ths slave element
			FESurfaceElement& se = ss.Element(i);

			bool sporo, ssolu;
			BiphasicSoluteStatus(*pm, se, sporo, ssolu);
			
			// get nr of nodes and integration points
			int nseln = se.Nodes();
			int nint = se.GaussPoints();

			double pn[4], cn[4];
			for (j=0; j<4; ++j)
			{
				pn[j] = ss.GetMesh()->Node(se.m_node[j]).m_pt;
				cn[j] = ss.GetMesh()->Node(se.m_node[j]).m_ct[0];
			}
			
			// get the element's LM vector
			ss.UnpackLM(se, sLM);
			
			// we calculate all the metrics we need before we
			// calculate the nodal forces
			for (j=0; j<nint; ++j)
			{
				// get the base vectors
				vec3d g[2];
				ss.CoBaseVectors(se, j, g);
				
				// jacobians: J = |g0xg1|
				detJ[j] = (g[0] ^ g[1]).norm();
				
				// integration weights
				w[j] = se.GaussWeights()[j];
				
				// pressure
				if (sporo)
				{
					pt[j] = se.eval(pn, j);
					dpr[j] = se.eval_deriv1(pn, j);
					dps[j] = se.eval_deriv2(pn, j);
				}
				// concentration
				if (ssolu)
				{
					ct[j] = se.eval(cn, j);
					dcr[j] = se.eval_deriv1(cn, j);
					dcs[j] = se.eval_deriv2(cn, j);
				}
			}
			
			// loop over all integration points
			for (j=0; j<nint; ++j, ++ni)
			{
				// get the master element
				FESurfaceElement* pme = ss.m_pme[ni];
				if (pme)
				{
					FESurfaceElement& me = *pme;

					bool mporo, msolu;
					BiphasicSoluteStatus(*pm, me, mporo, msolu);
					
					// get the nr of master nodes
					int nmeln = me.Nodes();

					// nodal pressures
					double pm[4], cm[4];
					for (k=0; k<nmeln; ++k) 
					{
						pm[k] = ms.GetMesh()->Node(me.m_node[k]).m_pt;
						cm[k] = ms.GetMesh()->Node(me.m_node[k]).m_ct[0];
					}
					
					// get the element's LM vector
					ms.UnpackLM(me, mLM);
					
					int ndpn;	// number of dofs per node
					int ndof;	// number of dofs in stiffness matrix
					
					if (ssolu && msolu) {
						// calculate dofs for biphasic-solute contact
						ndpn = 5;
						ndof = ndpn*(nseln+nmeln);
						
						// build the LM vector
						LM.resize(ndof);
						
						for (k=0; k<nseln; ++k)
						{
							LM[ndpn*k  ] = sLM[3*k  ];			// x-dof
							LM[ndpn*k+1] = sLM[3*k+1];			// y-dof
							LM[ndpn*k+2] = sLM[3*k+2];			// z-dof
							LM[ndpn*k+3] = sLM[3*nseln+k];		// p-dof
							LM[ndpn*k+4] = sLM[11*nseln+k];		// c-dof
						}
						for (k=0; k<nmeln; ++k)
						{
							LM[ndpn*(k+nseln)  ] = mLM[3*k  ];			// x-dof
							LM[ndpn*(k+nseln)+1] = mLM[3*k+1];			// y-dof
							LM[ndpn*(k+nseln)+2] = mLM[3*k+2];			// z-dof
							LM[ndpn*(k+nseln)+3] = mLM[3*nmeln+k];		// p-dof
							LM[ndpn*(k+nseln)+4] = mLM[11*nmeln+k];		// c-dof
						}
					}
					
					else if (sporo && mporo) {
						// calculate dofs for biphasic contact
						ndpn = 4;
						ndof = ndpn*(nseln+nmeln);
						
						// build the LM vector
						LM.resize(ndof);
						
						for (k=0; k<nseln; ++k)
						{
							LM[ndpn*k  ] = sLM[3*k  ];			// x-dof
							LM[ndpn*k+1] = sLM[3*k+1];			// y-dof
							LM[ndpn*k+2] = sLM[3*k+2];			// z-dof
							LM[ndpn*k+3] = sLM[3*nseln+k];		// p-dof
						}
						for (k=0; k<nmeln; ++k)
						{
							LM[ndpn*(k+nseln)  ] = mLM[3*k  ];			// x-dof
							LM[ndpn*(k+nseln)+1] = mLM[3*k+1];			// y-dof
							LM[ndpn*(k+nseln)+2] = mLM[3*k+2];			// z-dof
							LM[ndpn*(k+nseln)+3] = mLM[3*nmeln+k];		// p-dof
						}
					}
					
					else {
						// calculate dofs for elastic contact
						ndpn = 3;
						ndof = ndpn*(nseln + nmeln);
						
						// build the LM vector
						LM.resize(ndof);
						
						for (k=0; k<nseln; ++k)
						{
							LM[3*k  ] = sLM[3*k  ];
							LM[3*k+1] = sLM[3*k+1];
							LM[3*k+2] = sLM[3*k+2];
						}
						
						for (k=0; k<nmeln; ++k)
						{
							LM[3*(k+nseln)  ] = mLM[3*k  ];
							LM[3*(k+nseln)+1] = mLM[3*k+1];
							LM[3*(k+nseln)+2] = mLM[3*k+2];
						}
					}
					
					// build the en vector
					en.resize(nseln+nmeln);
					for (k=0; k<nseln; ++k) en[k      ] = se.m_node[k];
					for (k=0; k<nmeln; ++k) en[k+nseln] = me.m_node[k];
					
					// slave shape functions
					Hs = se.H(j);
					
					// master shape functions
					double r = ss.m_rs[ni][0];
					double s = ss.m_rs[ni][1];
					me.shape_fnc(Hm, r, s);
					
					// get slave normal vector
					vec3d nu = ss.m_nu[ni];
					
					// gap function
					double g = ss.m_gap[ni];
					
					// lagrange multiplier
					double Lm = ss.m_Lmd[ni];
					
					// penalty 
					double eps = m_epsn*ss.m_epsn[ni];
					
					// contact traction
					double tn = Lm + eps*g;
					tn = MBRACKET(tn);
					
					//	double dtn = m_eps*HEAVYSIDE(Lm + eps*g);
					double dtn = (tn > 0.? eps :0.);
					
					// create the stiffness matrix
					ke.resize(ndof, ndof); ke.zero();
					
					// --- S O L I D - S O L I D   C O N T A C T ---
					
					// a. NxN-term
					//------------------------------------
					
					// calculate the N-vector
					for (k=0; k<nseln; ++k)
					{
						N[ndpn*k  ] = Hs[k]*nu.x;
						N[ndpn*k+1] = Hs[k]*nu.y;
						N[ndpn*k+2] = Hs[k]*nu.z;
					}
					
					for (k=0; k<nmeln; ++k)
					{
						N[ndpn*(k+nseln)  ] = -Hm[k]*nu.x;
						N[ndpn*(k+nseln)+1] = -Hm[k]*nu.y;
						N[ndpn*(k+nseln)+2] = -Hm[k]*nu.z;
					}
					
					if (ndpn == 5) {
						for (k=0; k<nseln; ++k)
						{
							N[ndpn*k+3] = 0;
							N[ndpn*k+4] = 0;
						}
						for (k=0; k<nmeln; ++k)
						{
							N[ndpn*(k+nseln)+3] = 0;
							N[ndpn*(k+nseln)+4] = 0;
						}
					}
					else if (ndpn == 4) {
						for (k=0; k<nseln; ++k)
							N[ndpn*k+3] = 0;
						for (k=0; k<nmeln; ++k)
							N[ndpn*(k+nseln)+3] = 0;
					}
					
					for (k=0; k<ndof; ++k)
						for (l=0; l<ndof; ++l) ke[k][l] += dtn*N[k]*N[l]*detJ[j]*w[j];
					
					// b. A-term
					//-------------------------------------
					
					for (k=0; k<nseln; ++k) N[k      ] =  Hs[k];
					for (k=0; k<nmeln; ++k) N[k+nseln] = -Hm[k];
					
					double* Gr = se.Gr(j);
					double* Gs = se.Gs(j);
					vec3d gs[2];
					ss.CoBaseVectors(se, j, gs);
					
					mat3d S1, S2;
					S1.skew(gs[0]);
					S2.skew(gs[1]);
					mat3d As[4];
					for (l=0; l<nseln; ++l)
						As[l] = S2*Gr[l] - S1*Gs[l];
					
					if (!m_bsymm)
					{	// non-symmetric
						for (l=0; l<nseln; ++l)
						{
							for (k=0; k<nseln+nmeln; ++k)
							{
								ke[k*ndpn  ][l*ndpn  ] -= knmult*tn*w[j]*N[k]*As[l][0][0];
								ke[k*ndpn  ][l*ndpn+1] -= knmult*tn*w[j]*N[k]*As[l][0][1];
								ke[k*ndpn  ][l*ndpn+2] -= knmult*tn*w[j]*N[k]*As[l][0][2];
								
								ke[k*ndpn+1][l*ndpn  ] -= knmult*tn*w[j]*N[k]*As[l][1][0];
								ke[k*ndpn+1][l*ndpn+1] -= knmult*tn*w[j]*N[k]*As[l][1][1];
								ke[k*ndpn+1][l*ndpn+2] -= knmult*tn*w[j]*N[k]*As[l][1][2];
								
								ke[k*ndpn+2][l*ndpn  ] -= knmult*tn*w[j]*N[k]*As[l][2][0];
								ke[k*ndpn+2][l*ndpn+1] -= knmult*tn*w[j]*N[k]*As[l][2][1];
								ke[k*ndpn+2][l*ndpn+2] -= knmult*tn*w[j]*N[k]*As[l][2][2];
							}
						}
					} 
					else 
					{	// symmetric
						for (l=0; l<nseln; ++l)
						{
							for (k=0; k<nseln+nmeln; ++k)
							{
								ke[k*ndpn  ][l*ndpn  ] -= 0.5*knmult*tn*w[j]*N[k]*As[l][0][0];
								ke[k*ndpn  ][l*ndpn+1] -= 0.5*knmult*tn*w[j]*N[k]*As[l][0][1];
								ke[k*ndpn  ][l*ndpn+2] -= 0.5*knmult*tn*w[j]*N[k]*As[l][0][2];
								
								ke[k*ndpn+1][l*ndpn  ] -= 0.5*knmult*tn*w[j]*N[k]*As[l][1][0];
								ke[k*ndpn+1][l*ndpn+1] -= 0.5*knmult*tn*w[j]*N[k]*As[l][1][1];
								ke[k*ndpn+1][l*ndpn+2] -= 0.5*knmult*tn*w[j]*N[k]*As[l][1][2];
								
								ke[k*ndpn+2][l*ndpn  ] -= 0.5*knmult*tn*w[j]*N[k]*As[l][2][0];
								ke[k*ndpn+2][l*ndpn+1] -= 0.5*knmult*tn*w[j]*N[k]*As[l][2][1];
								ke[k*ndpn+2][l*ndpn+2] -= 0.5*knmult*tn*w[j]*N[k]*As[l][2][2];
								
								ke[l*ndpn  ][k*ndpn  ] -= 0.5*knmult*tn*w[j]*N[k]*As[l][0][0];
								ke[l*ndpn+1][k*ndpn  ] -= 0.5*knmult*tn*w[j]*N[k]*As[l][0][1];
								ke[l*ndpn+2][k*ndpn  ] -= 0.5*knmult*tn*w[j]*N[k]*As[l][0][2];
								
								ke[l*ndpn  ][k*ndpn+1] -= 0.5*knmult*tn*w[j]*N[k]*As[l][1][0];
								ke[l*ndpn+1][k*ndpn+1] -= 0.5*knmult*tn*w[j]*N[k]*As[l][1][1];
								ke[l*ndpn+2][k*ndpn+1] -= 0.5*knmult*tn*w[j]*N[k]*As[l][1][2];
								
								ke[l*ndpn  ][k*ndpn+2] -= 0.5*knmult*tn*w[j]*N[k]*As[l][2][0];
								ke[l*ndpn+1][k*ndpn+2] -= 0.5*knmult*tn*w[j]*N[k]*As[l][2][1];
								ke[l*ndpn+2][k*ndpn+2] -= 0.5*knmult*tn*w[j]*N[k]*As[l][2][2];
							}
						}
					}
					
					// c. M-term
					//---------------------------------------
					
					vec3d Gm[2];
					ms.ContraBaseVectors(me, r, s, Gm);
					
					// evaluate master surface normal
					vec3d mnu = Gm[0] ^ Gm[1];
					mnu.unit();
					
					double Hmr[4], Hms[4];
					me.shape_deriv(Hmr, Hms, r, s);
					vec3d mm[4];
					for (k=0; k<nmeln; ++k) 
						mm[k] = Gm[0]*Hmr[k] + Gm[1]*Hms[k];
					
					if (!m_bsymm)
					{	// non-symmetric
						for (k=0; k<nmeln; ++k) 
						{
							for (l=0; l<nseln+nmeln; ++l)
							{
								ke[(k+nseln)*ndpn  ][l*ndpn  ] += tn*knmult*detJ[j]*w[j]*mnu.x*mm[k].x*N[l];
								ke[(k+nseln)*ndpn  ][l*ndpn+1] += tn*knmult*detJ[j]*w[j]*mnu.x*mm[k].y*N[l];
								ke[(k+nseln)*ndpn  ][l*ndpn+2] += tn*knmult*detJ[j]*w[j]*mnu.x*mm[k].z*N[l];
								
								ke[(k+nseln)*ndpn+1][l*ndpn  ] += tn*knmult*detJ[j]*w[j]*mnu.y*mm[k].x*N[l];
								ke[(k+nseln)*ndpn+1][l*ndpn+1] += tn*knmult*detJ[j]*w[j]*mnu.y*mm[k].y*N[l];
								ke[(k+nseln)*ndpn+1][l*ndpn+2] += tn*knmult*detJ[j]*w[j]*mnu.y*mm[k].z*N[l];
								
								ke[(k+nseln)*ndpn+2][l*ndpn  ] += tn*knmult*detJ[j]*w[j]*mnu.z*mm[k].x*N[l];
								ke[(k+nseln)*ndpn+2][l*ndpn+1] += tn*knmult*detJ[j]*w[j]*mnu.z*mm[k].y*N[l];
								ke[(k+nseln)*ndpn+2][l*ndpn+2] += tn*knmult*detJ[j]*w[j]*mnu.z*mm[k].z*N[l];
							}
						}
					}
					else
					{	// symmetric
						for (k=0; k<nmeln; ++k) 
						{
							for (l=0; l<nseln+nmeln; ++l)
							{
								ke[(k+nseln)*ndpn  ][l*ndpn  ] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.x*mm[k].x*N[l];
								ke[(k+nseln)*ndpn  ][l*ndpn+1] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.x*mm[k].y*N[l];
								ke[(k+nseln)*ndpn  ][l*ndpn+2] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.x*mm[k].z*N[l];
								
								ke[(k+nseln)*ndpn+1][l*ndpn  ] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.y*mm[k].x*N[l];
								ke[(k+nseln)*ndpn+1][l*ndpn+1] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.y*mm[k].y*N[l];
								ke[(k+nseln)*ndpn+1][l*ndpn+2] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.y*mm[k].z*N[l];
								
								ke[(k+nseln)*ndpn+2][l*ndpn  ] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.z*mm[k].x*N[l];
								ke[(k+nseln)*ndpn+2][l*ndpn+1] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.z*mm[k].y*N[l];
								ke[(k+nseln)*ndpn+2][l*ndpn+2] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.z*mm[k].z*N[l];
								
								ke[l*ndpn  ][(k+nseln)*ndpn  ] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.x*mm[k].x*N[l];
								ke[l*ndpn+1][(k+nseln)*ndpn  ] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.y*mm[k].x*N[l];
								ke[l*ndpn+2][(k+nseln)*ndpn  ] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.z*mm[k].x*N[l];
								
								ke[l*ndpn  ][(k+nseln)*ndpn+1] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.x*mm[k].y*N[l];
								ke[l*ndpn+1][(k+nseln)*ndpn+1] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.y*mm[k].y*N[l];
								ke[l*ndpn+2][(k+nseln)*ndpn+1] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.z*mm[k].y*N[l];
								
								ke[l*ndpn  ][(k+nseln)*ndpn+2] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.x*mm[k].z*N[l];
								ke[l*ndpn+1][(k+nseln)*ndpn+2] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.y*mm[k].z*N[l];
								ke[l*ndpn+2][(k+nseln)*ndpn+2] += 0.5*knmult*tn*detJ[j]*w[j]*mnu.z*mm[k].z*N[l];
							}
						}
					}
					
					// --- B I P H A S I C - S O L U T E  S T I F F N E S S ---
					if (ssolu && msolu)
					{
						double dt = fem.GetCurrentStep()->m_dt;
						
						double epsp = (tn > 0) ? m_epsp*ss.m_epsp[ni] : 0.;
						double epsc = (tn > 0) ? m_epsc*ss.m_epsc[ni] : 0.;
						
						// --- S O L I D - P R E S S U R E / S O L U T E   C O N T A C T ---
						
						if (!m_bsymm)
						{
							
							// a. q-term
							//-------------------------------------
							
							double dpmr, dpms;
							dpmr = me.eval_deriv1(pm, r, s);
							dpms = me.eval_deriv2(pm, r, s);
							
							double dcmr, dcms;
							dcmr = me.eval_deriv1(cm, r, s);
							dcms = me.eval_deriv2(cm, r, s);
							
							for (k=0; k<nseln+nmeln; ++k)
								for (l=0; l<nseln+nmeln; ++l)
								{
									ke[ndpn*k + 3][ndpn*l  ] += dt*w[j]*detJ[j]*epsp*N[k]*N[l]*(dpmr*Gm[0].x + dpms*Gm[1].x);
									ke[ndpn*k + 3][ndpn*l+1] += dt*w[j]*detJ[j]*epsp*N[k]*N[l]*(dpmr*Gm[0].y + dpms*Gm[1].y);
									ke[ndpn*k + 3][ndpn*l+2] += dt*w[j]*detJ[j]*epsp*N[k]*N[l]*(dpmr*Gm[0].z + dpms*Gm[1].z);

									ke[ndpn*k + 4][ndpn*l  ] += dt*w[j]*detJ[j]*epsc*N[k]*N[l]*(dcmr*Gm[0].x + dcms*Gm[1].x);
									ke[ndpn*k + 4][ndpn*l+1] += dt*w[j]*detJ[j]*epsc*N[k]*N[l]*(dcmr*Gm[0].y + dcms*Gm[1].y);
									ke[ndpn*k + 4][ndpn*l+2] += dt*w[j]*detJ[j]*epsc*N[k]*N[l]*(dcmr*Gm[0].z + dcms*Gm[1].z);
								}
							
							double wn = ss.m_Lmp[ni] + epsp*ss.m_pg[ni];
							double jn = ss.m_Lmc[ni] + epsc*ss.m_cg[ni];
							
							// b. A-term
							//-------------------------------------
							
							for (l=0; l<nseln; ++l)
								for (k=0; k<nseln+nmeln; ++k)
								{
									ke[ndpn*k + 3][ndpn*l  ] -= dt*w[j]*wn*N[k]*(As[l][0][0]*nu.x + As[l][0][1]*nu.y + As[l][0][2]*nu.z);
									ke[ndpn*k + 3][ndpn*l+1] -= dt*w[j]*wn*N[k]*(As[l][1][0]*nu.x + As[l][1][1]*nu.y + As[l][1][2]*nu.z);
									ke[ndpn*k + 3][ndpn*l+2] -= dt*w[j]*wn*N[k]*(As[l][2][0]*nu.x + As[l][2][1]*nu.y + As[l][2][2]*nu.z);

									ke[ndpn*k + 4][ndpn*l  ] -= dt*w[j]*jn*N[k]*(As[l][0][0]*nu.x + As[l][0][1]*nu.y + As[l][0][2]*nu.z);
									ke[ndpn*k + 4][ndpn*l+1] -= dt*w[j]*jn*N[k]*(As[l][1][0]*nu.x + As[l][1][1]*nu.y + As[l][1][2]*nu.z);
									ke[ndpn*k + 4][ndpn*l+2] -= dt*w[j]*jn*N[k]*(As[l][2][0]*nu.x + As[l][2][1]*nu.y + As[l][2][2]*nu.z);
								}
							
							// c. m-term
							//---------------------------------------
							
							for (k=0; k<nmeln; ++k)
								for (l=0; l<nseln+nmeln; ++l)
								{
									ke[ndpn*(k+nseln) + 3][ndpn*l  ] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].x;
									ke[ndpn*(k+nseln) + 3][ndpn*l+1] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].y;
									ke[ndpn*(k+nseln) + 3][ndpn*l+2] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].z;

									ke[ndpn*(k+nseln) + 4][ndpn*l  ] += dt*w[j]*detJ[j]*jn*N[l]*mm[k].x;
									ke[ndpn*(k+nseln) + 4][ndpn*l+1] += dt*w[j]*detJ[j]*jn*N[l]*mm[k].y;
									ke[ndpn*(k+nseln) + 4][ndpn*l+2] += dt*w[j]*detJ[j]*jn*N[l]*mm[k].z;
								}
						}
						
						
						// --- P R E S S U R E - P R E S S U R E   C O N T A C T ---
						
						// calculate the N-vector
						for (k=0; k<nseln; ++k)
						{
							N[ndpn*k+3] = Hs[k];
							N[ndpn*k+4] = Hs[k];
						}
						
						for (k=0; k<nmeln; ++k)
						{
							N[ndpn*(k+nseln)+3] = -Hm[k];
							N[ndpn*(k+nseln)+4] = -Hm[k];
						}
						
						for (k=3; k<ndof; k+=ndpn)
							for (l=3; l<ndof; l+=ndpn) ke[k][l] -= dt*epsp*w[j]*detJ[j]*N[k]*N[l];

						// --- C O N C E N T R A T I O N - C O N C E N T R A T I O N   C O N T A C T ---
						
						for (k=4; k<ndof; k+=ndpn)
							for (l=4; l<ndof; l+=ndpn) ke[k][l] -= dt*epsc*w[j]*detJ[j]*N[k]*N[l];
						
					}
					
					// --- B I P H A S I C   S T I F F N E S S ---
					else if (sporo && mporo)
					{
						// the variable dt is either the timestep or one
						// depending on whether we are using the symmetric
						// poro version or not.
						double dt = fem.GetCurrentStep()->m_dt;
						
						double epsp = (tn > 0) ? m_epsp*ss.m_epsp[ni] : 0.;
						
						// --- S O L I D - P R E S S U R E   C O N T A C T ---
						
						if (!m_bsymm)
						{
							
							// a. q-term
							//-------------------------------------
							
							double dpmr, dpms;
							dpmr = me.eval_deriv1(pm, r, s);
							dpms = me.eval_deriv2(pm, r, s);
							
							for (k=0; k<nseln+nmeln; ++k)
								for (l=0; l<nseln+nmeln; ++l)
								{
									ke[ndpn*k + 3][ndpn*l  ] += dt*w[j]*detJ[j]*epsp*N[k]*N[l]*(dpmr*Gm[0].x + dpms*Gm[1].x);
									ke[ndpn*k + 3][ndpn*l+1] += dt*w[j]*detJ[j]*epsp*N[k]*N[l]*(dpmr*Gm[0].y + dpms*Gm[1].y);
									ke[ndpn*k + 3][ndpn*l+2] += dt*w[j]*detJ[j]*epsp*N[k]*N[l]*(dpmr*Gm[0].z + dpms*Gm[1].z);
								}
							
							double wn = ss.m_Lmp[ni] + epsp*ss.m_pg[ni];
							
							// b. A-term
							//-------------------------------------
							
							for (l=0; l<nseln; ++l)
								for (k=0; k<nseln+nmeln; ++k)
								{
									ke[ndpn*k + 3][ndpn*l  ] -= dt*w[j]*wn*N[k]*(As[l][0][0]*nu.x + As[l][0][1]*nu.y + As[l][0][2]*nu.z);
									ke[ndpn*k + 3][ndpn*l+1] -= dt*w[j]*wn*N[k]*(As[l][1][0]*nu.x + As[l][1][1]*nu.y + As[l][1][2]*nu.z);
									ke[ndpn*k + 3][ndpn*l+2] -= dt*w[j]*wn*N[k]*(As[l][2][0]*nu.x + As[l][2][1]*nu.y + As[l][2][2]*nu.z);
								}
							
							// c. m-term
							//---------------------------------------
							
							for (k=0; k<nmeln; ++k)
								for (l=0; l<nseln+nmeln; ++l)
								{
									ke[ndpn*(k+nseln) + 3][ndpn*l  ] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].x;
									ke[ndpn*(k+nseln) + 3][ndpn*l+1] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].y;
									ke[ndpn*(k+nseln) + 3][ndpn*l+2] += dt*w[j]*detJ[j]*wn*N[l]*mm[k].z;
								}
						}
						
						
						// --- P R E S S U R E - P R E S S U R E   C O N T A C T ---
						
						// calculate the N-vector
						for (k=0; k<nseln; ++k)
						{
							N[ndpn*k+3] = Hs[k];
						}
						
						for (k=0; k<nmeln; ++k)
						{
							N[ndpn*(k+nseln)+3] = -Hm[k];
						}
						
						for (k=3; k<ndof; k+=ndpn)
							for (l=3; l<ndof; l+=ndpn) ke[k][l] -= dt*epsp*w[j]*detJ[j]*N[k]*N[l];
						
					}
					
					// assemble the global stiffness
					psolver->AssembleStiffness(en, LM, ke);
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::UpdateContactPressures()
{
	int np, n, i, j, k;
	int npass = (m_btwo_pass?2:1);
	for (np=0; np<npass; ++np)
	{
		FESlidingSurface3& ss = (np == 0? m_ss : m_ms);
		FESlidingSurface3& ms = (np == 0? m_ms : m_ss);
		
		// keep a running counter of integration points.
		int ni = 0;
		
		// loop over all elements of the primary surface
		for (n=0; n<ss.Elements(); ++n)
		{
			FESurfaceElement& el = ss.Element(n);
			int nint = el.GaussPoints();
			
			// get the normal tractions at the integration points
			double gap, eps;
			for (i=0; i<nint; ++i, ++ni) 
			{
				gap = ss.m_gap[ni];
				eps = m_epsn*ss.m_epsn[ni];
				ss.m_Ln[ni] = MBRACKET(ss.m_Lmd[ni] + eps*gap);
				FESurfaceElement* pme = ss.m_pme[ni];
				if (m_btwo_pass && pme)
				{
					int mint = pme->GaussPoints();
					int noff = ms.m_nei[pme->m_lid];
					double ti[4];
					for (j=0; j<mint; ++j) {
						k = noff+j;
						gap = ms.m_gap[k];
						eps = m_epsn*ms.m_epsn[k];
						ti[j] = MBRACKET(ms.m_Lmd[k] + m_epsn*ms.m_epsn[k]*ms.m_gap[k]);
					}
					// project the data to the nodes
					double tn[4];
					pme->project_to_nodes(ti, tn);
					// now evaluate the traction at the intersection point
					double Ln = pme->eval(tn, ss.m_rs[ni][0], ss.m_rs[ni][1]);
					ss.m_Ln[ni] += MBRACKET(Ln);
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
bool FESlidingInterface3::Augment(int naug)
{
	// make sure we need to augment
	if (!m_blaugon) return true;
	
	int i;
	double Ln, Lp, Lc;
	bool bconv = true;
	
	bool bporo = (m_ss.m_bporo && m_ms.m_bporo);
	bool bsolu = (m_ss.m_bsolu && m_ms.m_bsolu);
	int NS = m_ss.m_Lmd.size();
	int NM = m_ms.m_Lmd.size();
	
	// --- c a l c u l a t e   i n i t i a l   n o r m s ---
	// a. normal component
	double normL0 = 0, normP = 0, normDP = 0, normC = 0, normDC = 0;
	for (i=0; i<NS; ++i) normL0 += m_ss.m_Lmd[i]*m_ss.m_Lmd[i];
	for (i=0; i<NM; ++i) normL0 += m_ms.m_Lmd[i]*m_ms.m_Lmd[i];
	
	// b. gap component
	// (is calculated during update)
	double maxgap = 0, maxpg = 0, maxcg = 0;
	
	// update Lagrange multipliers
	double normL1 = 0, eps, epsp, epsc;
	for (i=0; i<NS; ++i)
	{
		// update Lagrange multipliers on slave surface
		eps = m_epsn*m_ss.m_epsn[i];
		Ln = m_ss.m_Lmd[i] + eps*m_ss.m_gap[i];
		m_ss.m_Lmd[i] = MBRACKET(Ln);
		
		normL1 += m_ss.m_Lmd[i]*m_ss.m_Lmd[i];
		
		if (m_ss.m_bporo) {
			Lp = Lc = 0;
			if (Ln > 0) {
				epsp = m_epsp*m_ss.m_epsp[i];
				Lp = m_ss.m_Lmp[i] + epsp*m_ss.m_pg[i];
				maxpg = max(maxpg,fabs(m_ss.m_pg[i]));
				normDP += m_ss.m_pg[i]*m_ss.m_pg[i];
				epsc = m_epsc*m_ss.m_epsc[i];
				Lc = m_ss.m_Lmc[i] + epsc*m_ss.m_cg[i];
				maxcg = max(maxcg,fabs(m_ss.m_cg[i]));
				normDC += m_ss.m_cg[i]*m_ss.m_cg[i];
			}
			m_ss.m_Lmp[i] = Lp;
			m_ss.m_Lmc[i] = Lc;
		}
		
		if (Ln > 0) maxgap = max(maxgap,fabs(m_ss.m_gap[i]));
	}	
	
	for (i=0; i<NM; ++i)
	{
		// update Lagrange multipliers on master surface
		eps = m_epsn*m_ms.m_epsn[i];
		Ln = m_ms.m_Lmd[i] + eps*m_ms.m_gap[i];
		m_ms.m_Lmd[i] = MBRACKET(Ln);
		
		normL1 += m_ms.m_Lmd[i]*m_ms.m_Lmd[i];
		
		if (m_ms.m_bporo) {
			Lp = Lc = 0;
			if (Ln > 0) {
				epsp = m_epsp*m_ms.m_epsp[i];
				Lp = m_ms.m_Lmp[i] + epsp*m_ms.m_pg[i];
				maxpg = max(maxpg,fabs(m_ms.m_pg[i]));
				normDP += m_ms.m_pg[i]*m_ms.m_pg[i];
				epsc = m_epsc*m_ms.m_epsc[i];
				Lc = m_ms.m_Lmc[i] + epsc*m_ms.m_cg[i];
				maxcg = max(maxcg,fabs(m_ms.m_cg[i]));
				normDC += m_ms.m_cg[i]*m_ms.m_cg[i];
			}
			m_ms.m_Lmp[i] = Lp;
			m_ms.m_Lmc[i] = Lc;
		}
		
		if (Ln > 0) maxgap = max(maxgap,fabs(m_ms.m_gap[i]));
	}
	
	// normP should be a measure of the fluid pressure at the
	// contact interface.  However, since it could be zero,
	// use an average measure of the contact traction instead.
	normP = normL1;
	normC = normL1/(m_Rgas*m_Tabs);
	
	// calculate relative norms
	double lnorm = (normL1 != 0 ? fabs((normL1 - normL0) / normL1) : fabs(normL1 - normL0)); 
	double pnorm = (normP != 0 ? (normDP/normP) : normDP); 
	double cnorm = (normC != 0 ? (normDC/normC) : normDC); 
	
	// check convergence
	if ((m_gtol > 0) && (maxgap > m_gtol)) bconv = false;
	if ((m_ptol > 0) && (bporo && maxpg > m_ptol)) bconv = false;
	if ((m_ctol > 0) && (bsolu && maxcg > m_ctol)) bconv = false;
	
	if ((m_atol > 0) && (lnorm > m_atol)) bconv = false;
	if ((m_atol > 0) && (pnorm > m_atol)) bconv = false;
	if ((m_atol > 0) && (cnorm > m_atol)) bconv = false;
	
	clog.printf(" sliding interface # %d\n", m_nID);
	clog.printf("                        CURRENT        REQUIRED\n");
	clog.printf("    D multiplier : %15le", lnorm); if (m_atol > 0) clog.printf("%15le\n", m_atol); else clog.printf("       ***\n");
	if (bporo) { clog.printf("    P gap       : %15le", pnorm); if (m_atol > 0) clog.printf("%15le\n", m_atol); else clog.printf("       ***\n"); }
	if (bsolu) { clog.printf("    C gap       : %15le", cnorm); if (m_atol > 0) clog.printf("%15le\n", m_atol); else clog.printf("       ***\n"); }
	
	clog.printf("    maximum gap  : %15le", maxgap);
	if (m_gtol > 0) clog.printf("%15le\n", m_gtol); else clog.printf("       ***\n");
	if (bporo) {
		clog.printf("    maximum pgap : %15le", maxpg);
		if (m_ptol > 0) clog.printf("%15le\n", m_ptol); else clog.printf("       ***\n");
	}
	if (bsolu) {
		clog.printf("    maximum cgap : %15le", maxcg);
		if (m_ctol > 0) clog.printf("%15le\n", m_ctol); else clog.printf("       ***\n");
	}
	
	return bconv;
}

//-----------------------------------------------------------------------------
void FESlidingInterface3::Serialize(DumpFile &ar)
{
	FEContactInterface::Serialize(ar);
	if (ar.IsSaving())
	{
		ar << m_knmult;
		ar << m_btwo_pass;
		ar << m_atol;
		ar << m_gtol;
		ar << m_ptol;
		ar << m_ctol;
		ar << m_stol;
		ar << m_bsymm;
		ar << m_srad;
		ar << m_naugmax;
		ar << m_naugmin;
		ar << m_nsegup;
		ar << m_epsn;
		ar << m_bautopen;
		ar << m_epsp;
		ar << m_epsc;
		
		m_ms.Serialize(ar);
		m_ss.Serialize(ar);
	}
	else
	{
		ar >> m_knmult;
		ar >> m_btwo_pass;
		ar >> m_atol;
		ar >> m_gtol;
		ar >> m_ptol;
		ar >> m_ctol;
		ar >> m_stol;
		ar >> m_bsymm;
		ar >> m_srad;
		ar >> m_naugmax;
		ar >> m_naugmin;
		ar >> m_nsegup;
		ar >> m_epsn;
		ar >> m_bautopen;
		ar >> m_epsp;
		ar >> m_epsc;
		
		m_ms.Serialize(ar);
		m_ss.Serialize(ar);
	}
}

//-----------------------------------------------------------------------------

void FESlidingInterface3::BiphasicSoluteStatus(FEMesh& m, FESurfaceElement& el, bool& bstat, bool& sstat)
{
	bstat = sstat = false;
	
	// get the solid element this surface element belongs to
	FESolidElement* pe = dynamic_cast<FESolidElement*>(m.FindElementFromID(el.m_nelem));
	if (pe)
	{
		// get the material
		FEMaterial* pm = dynamic_cast<FEMaterial*>(m_pfem->GetMaterial(pe->GetMatID()));
		
		// see if this is a poro-elastic element
		FEBiphasic* biph = dynamic_cast<FEBiphasic*> (pm);
		FEBiphasicSolute* bsolu = dynamic_cast<FEBiphasicSolute*> (pm);
		if (biph || bsolu) bstat = true;
		if (bsolu) sstat = true;
	}
	
	return;
}

//-----------------------------------------------------------------------------

void FESlidingInterface3::MarkAmbient()
{	
	int i, id, np;
	
	// Mark all nodes as free-draining.  This needs to be done for ALL
	// contact interfaces prior to executing Update(), where nodes that are
	// in contact are subsequently marked as non free-draining.  This ensures
	// that for surfaces involved in more than one contact interface, nodes
	// that have been marked as non free-draining are not reset to 
	// free-draining.
	for (np=0; np<2; ++np)
	{
		FESlidingSurface3& s = (np == 0? m_ss : m_ms);
		
		if (s.m_bporo) {
			// first, mark all nodes as free-draining (= neg. ID)
			// this is done by setting the dof's equation number
			// to a negative number
			for (i=0; i<s.Nodes(); ++i) 
			{
				id = s.Node(i).m_ID[DOF_P];
				if (id >= 0) 
				{
					FENode& node = s.Node(i);
					// mark node as free-draining
					node.m_ID[DOF_P] = -id-2;
				}
			}
		}
		if (s.m_bsolu) {
			// first, mark all nodes as free-draining (= neg. ID)
			// this is done by setting the dof's equation number
			// to a negative number
			for (i=0; i<s.Nodes(); ++i) 
			{
				id = s.Node(i).m_ID[DOF_C];
				if (id >= 0) 
				{
					FENode& node = s.Node(i);
					// mark node as free-draining
					node.m_ID[DOF_C] = -id-2;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------

void FESlidingInterface3::SetAmbient()
{	
	int i, np;

	// Set the pressure to zero for the free-draining nodes
	for (np=0; np<2; ++np)
	{
		FESlidingSurface3& s = (np == 0? m_ss : m_ms);
		
		if (s.m_bporo) {
			// loop over all nodes
			for (i=0; i<s.Nodes(); ++i) 
			{
				if (s.Node(i).m_ID[DOF_P] < -1)
				{
					FENode& node = s.Node(i);
					// set the fluid pressure to ambient condition
					node.m_pt = m_ambp;
				}
			}
		}
		if (s.m_bsolu) {
			// loop over all nodes
			for (i=0; i<s.Nodes(); ++i) 
			{
				if (s.Node(i).m_ID[DOF_C] < -1)
				{
					FENode& node = s.Node(i);
					// set the fluid pressure to ambient condition
					node.m_ct[0] = m_ambc;
				}
			}
		}
	}
}
