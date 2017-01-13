#include "stdafx.h"
#include "FERigidSphereContact.h"
#include <FECore/FEShellDomain.h>
#include <FECore/FEModel.h>
#include <FECore/FEGlobalMatrix.h>
#include <FECore/log.h>

//-----------------------------------------------------------------------------
// Define sliding interface parameters
BEGIN_PARAMETER_LIST(FERigidSphereContact, FEContactInterface)
	ADD_PARAMETER(m_blaugon , FE_PARAM_BOOL  , "laugon"      ); 
	ADD_PARAMETER(m_atol    , FE_PARAM_DOUBLE, "tolerance"   );
	ADD_PARAMETER(m_eps     , FE_PARAM_DOUBLE, "penalty"     );
	ADD_PARAMETER(m_sphere.m_R , FE_PARAM_DOUBLE, "radius" );
	ADD_PARAMETER(m_sphere.m_rc, FE_PARAM_VEC3D, "center");
	ADD_PARAMETER(m_sphere.m_uc.x, FE_PARAM_DOUBLE, "ux");
	ADD_PARAMETER(m_sphere.m_uc.y, FE_PARAM_DOUBLE, "uy");
	ADD_PARAMETER(m_sphere.m_uc.z, FE_PARAM_DOUBLE, "uz");
END_PARAMETER_LIST();

///////////////////////////////////////////////////////////////////////////////
// FERigidSphereSurface
///////////////////////////////////////////////////////////////////////////////


FERigidSphereSurface::FERigidSphereSurface(FEModel* pfem) : FESurface(&pfem->GetMesh())
{ 
	m_NQ.Attach(this); 

	// I want to use the FEModel class for this, but don't know how
	DOFS& dofs = pfem->GetDOFS();
	m_dofX = dofs.GetDOF("x");
	m_dofY = dofs.GetDOF("y");
	m_dofZ = dofs.GetDOF("z");
}

//-----------------------------------------------------------------------------
//! Creates a surface for use with a sliding interface. All surface data
//! structures are allocated.
//! Note that it is assumed that the element array is already created
//! and initialized.

bool FERigidSphereSurface::Init()
{
	// always intialize base class first!
	if (FESurface::Init() == false) return false;

	// get the number of elements
	int NE = Elements();

	// count total number of integration points
	int nintTotal = 0;
	for (int i=0; i<NE; ++i)
	{
		FESurfaceElement& el = Element(i);
		nintTotal += el.GaussPoints();
	}

	// allocate other surface data
	m_data.resize(nintTotal);
	for (int i=0; i<nintTotal; ++i)
	{
		DATA& d = m_data[i];
		d.gap = 0.0;
		d.Lm = 0.0;
		d.nu = vec3d(0,0,0);
	}

	return true;
}


//-----------------------------------------------------------------------------
// TODO: I don't think we need this
vec3d FERigidSphereSurface::traction(int inode)
{
	return vec3d(0,0,0);
}

//-----------------------------------------------------------------------------
void FERigidSphereSurface::Serialize(DumpStream &ar)
{
	FESurface::Serialize(ar);
	if (ar.IsSaving())
	{
		int nsize = (int)m_data.size();
		for (int i=0; i<nsize; ++i)
		{
			DATA& d = m_data[i];
			ar << d.gap;
			ar << d.nu;
			ar << d.Lm;
		}
	}
	else
	{
		int nsize = (int)m_data.size();
		for (int i = 0; i<nsize; ++i)
		{
			DATA& d = m_data[i];
			ar >> d.gap;
			ar >> d.nu;
			ar >> d.Lm;
		}
	}
}

//-----------------------------------------------------------------------------
void FERigidSphereSurface::UnpackLM(FEElement& el, vector<int>& lm)
{
	int N = el.Nodes();
	lm.resize(N*3);
	for (int i=0; i<N; ++i)
	{
		int n = el.m_lnode[i];
		FENode& node = Node(n);
		vector<int>& id = node.m_ID;

		lm[3*i  ] = id[m_dofX];
		lm[3*i+1] = id[m_dofY];
		lm[3*i+2] = id[m_dofZ];
	}
}

///////////////////////////////////////////////////////////////////////////////
// FERigidSphereContact
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//! constructor
FERigidSphereContact::FERigidSphereContact(FEModel* pfem) : FEContactInterface(pfem), m_ss(pfem), m_sphere(pfem)
{
	static int count = 1;
	SetID(count++);

	m_eps = 0;
	m_atol = 0;
};

//-----------------------------------------------------------------------------
//! Initializes the rigid wall interface data

bool FERigidSphereContact::Init()
{
	// create the surface
	if (m_ss.Init() == false) return false;

	// initialize rigid surface
	m_sphere.Init();

	return true;
}

//-----------------------------------------------------------------------------
//! build the matrix profile for use in the stiffness matrix
void FERigidSphereContact::BuildMatrixProfile(FEGlobalMatrix& K)
{
	FEModel& fem = *GetFEModel();

	// get the DOFS
	const int dof_X = fem.GetDOFIndex("x");
	const int dof_Y = fem.GetDOFIndex("y");
	const int dof_Z = fem.GetDOFIndex("z");
	const int dof_RU = fem.GetDOFIndex("Ru");
	const int dof_RV = fem.GetDOFIndex("Rv");
	const int dof_RW = fem.GetDOFIndex("Rw");

	int c = 0;
	vector<int> lm;
	lm.reserve(3*FEElement::MAX_NODES);
	for (int i=0; i<m_ss.Elements(); ++i)
	{
		FESurfaceElement& el = m_ss.Element(i);

		// see if any integration points are in contact
		for (int j=0; j<el.GaussPoints(); ++j)
		{
			FERigidSphereSurface::DATA& d = m_ss.m_data[c + j];
			if (d.gap >= 0)
			{
				m_ss.UnpackLM(el, lm);
				K.build_add(lm);
				break;
			}
		}
		c += el.GaussPoints();
	}
}

//-----------------------------------------------------------------------------
void FERigidSphereContact::Activate()
{
	// don't forget to call the base class
	FEContactInterface::Activate();

	// project slave surface onto master surface
	ProjectSurface(m_ss);
}

//-----------------------------------------------------------------------------
//!  Projects the slave surface onto the master plane

void FERigidSphereContact::ProjectSurface(FERigidSphereSurface& ss)
{
	vec3d rt[FEElement::MAX_NODES];

	// surface normal
	vec3d np;

	// loop over all slave elements
	int c = 0;
	for (int i=0; i<m_ss.Elements(); ++i)
	{
		FESurfaceElement& el = m_ss.Element(i);
		int neln = el.Nodes();
		int nint = el.GaussPoints();

		// get the nodal coordinates
		for (int j=0; j<neln; ++j) rt[j] = m_ss.Node(el.m_lnode[j]).m_rt;

		// loop over all integration points
		for (int j=0; j<nint; ++j)
		{
			// get the integration point data
			FERigidSphereSurface::DATA& d = m_ss.m_data[c++];

			// get the nodal position
			vec3d r = el.Evaluate(rt, j);

			// project this node onto the plane
			vec3d q = m_sphere.Project(r);

			// get the local surface normal
			vec3d np = m_sphere.Normal(q);

			// the slave normal is set to the master element normal
			d.nu = np;
	
			// calculate initial gap
			d.gap = -(np*(r - q));
		}
	}
}

//-----------------------------------------------------------------------------
//!  Updates rigid wall data

void FERigidSphereContact::Update(int niter)
{
	// project slave surface onto master surface
	ProjectSurface(m_ss);
}

//-----------------------------------------------------------------------------

void FERigidSphereContact::ContactForces(FEGlobalVector& R)
{
	vector<int> lm;
	const int MELN = FEElement::MAX_NODES;
	double detJ[MELN], w[MELN];
	vec3d r0[MELN];
	vector<double> fe;

	// loop over all slave elements
	int c = 0;
	for (int i = 0; i<m_ss.Elements(); ++i)
	{
		FESurfaceElement& se = m_ss.Element(i);
		int neln = se.Nodes();
		int nint = se.GaussPoints();
		int ndof = 3*neln;

		// get the element's LM vector
		m_ss.UnpackLM(se, lm);

		// nodal coordinates
		for (int j = 0; j<neln; ++j) r0[j] = m_ss.Node(se.m_lnode[j]).m_r0;

		// we calculate all the metrics we need before we
		// calculate the nodal forces
		for (int j = 0; j<nint; ++j)
		{
			double* Gr = se.Gr(j);
			double* Gs = se.Gs(j);

			// calculate jacobian
			// note that we are integrating over the reference surface
			vec3d dxr, dxs;
			for (int k = 0; k<neln; ++k)
			{
				dxr.x += Gr[k] * r0[k].x;
				dxr.y += Gr[k] * r0[k].y;
				dxr.z += Gr[k] * r0[k].z;

				dxs.x += Gs[k] * r0[k].x;
				dxs.y += Gs[k] * r0[k].y;
				dxs.z += Gs[k] * r0[k].z;
			}

			// jacobians
			detJ[j] = (dxr ^ dxs).norm();

			// integration weights
			w[j] = se.GaussWeights()[j];
		}

		// loop over all integration points
		for (int j = 0; j<nint; ++j)
		{
			// get integration point data
			FERigidSphereSurface::DATA& d = m_ss.m_data[c++];

			// calculate shape functions
			double* H = se.H(j);

			// get normal vector
			vec3d nu = d.nu;

			// gap function
			double g = d.gap;

			// lagrange multiplier
			double Lm = d.Lm;

			// penalty value
			double eps = m_eps;

			// contact traction
			double tn = Lm + eps*g;
			tn = MBRACKET(tn);

			// calculate the force vector
			fe.resize(ndof);

			for (int k = 0; k<neln; ++k)
			{
				fe[3 * k    ] = H[k] * nu.x;
				fe[3 * k + 1] = H[k] * nu.y;
				fe[3 * k + 2] = H[k] * nu.z;
			}
			for (int k = 0; k<ndof; ++k) fe[k] *= tn*detJ[j] * w[j];

			// assemble the global residual
			R.Assemble(se.m_node, lm, fe);
		}
	}
}

//-----------------------------------------------------------------------------
void FERigidSphereContact::ContactStiffness(FESolver* psolver)
{
	vector<int> lm;
	const int MELN = FEElement::MAX_NODES;
	const int MINT = FEElement::MAX_INTPOINTS;
	vec3d r0[MELN];
	double detJ[MINT], w[MINT];
	double N[3*MELN];
	matrix ke;

	// loop over all slave elements
	int c = 0;
	for (int i = 0; i<m_ss.Elements(); ++i)
	{
		FESurfaceElement& se = m_ss.Element(i);
		int neln = se.Nodes();
		int nint = se.GaussPoints();
		int ndof = 3*neln;

		// get the element's LM vector
		m_ss.UnpackLM(se, lm);

		// nodal coordinates
		for (int j = 0; j<neln; ++j) r0[j] = m_ss.Node(se.m_lnode[j]).m_r0;

		// we calculate all the metrics we need before we
		// calculate the nodal forces
		for (int j = 0; j<nint; ++j)
		{
			double* Gr = se.Gr(j);
			double* Gs = se.Gs(j);

			// calculate jacobian
			// note that we are integrating over the reference surface
			vec3d dxr, dxs;
			for (int k = 0; k<neln; ++k)
			{
				dxr.x += Gr[k] * r0[k].x;
				dxr.y += Gr[k] * r0[k].y;
				dxr.z += Gr[k] * r0[k].z;

				dxs.x += Gs[k] * r0[k].x;
				dxs.y += Gs[k] * r0[k].y;
				dxs.z += Gs[k] * r0[k].z;
			}

			// jacobians
			detJ[j] = (dxr ^ dxs).norm();

			// integration weights
			w[j] = se.GaussWeights()[j];
		}

		// loop over all integration points
		for (int j = 0; j<nint; ++j)
		{
			// get integration point data
			FERigidSphereSurface::DATA& d = m_ss.m_data[c++];

			// calculate shape functions
			double* H = se.H(j);

			// get normal vector
			vec3d nu = d.nu;

			// gap function
			double g = d.gap;

			// when the node is on the surface, the gap value
			// can flip-flop between positive and negative.
			if (fabs(g)<1e-20) g = 0;

			// lagrange multiplier
			double Lm = d.Lm;

			// penalty value
			double eps = m_eps;

			// contact traction
			double tn = Lm + eps*g;
			tn = MBRACKET(tn);

			double dtn = eps*HEAVYSIDE(Lm + eps*g);

			// calculate the N-vector
			for (int k = 0; k<neln; ++k)
			{
				N[3 * k    ] = H[k] * nu.x;
				N[3 * k + 1] = H[k] * nu.y;
				N[3 * k + 2] = H[k] * nu.z;
			}

			// create the stiffness matrix
			ke.resize(ndof, ndof);

			// add the first order term (= D(tn)*dg )
			for (int k = 0; k<ndof; ++k)
				for (int l = 0; l<ndof; ++l) ke[k][l] = dtn*N[k] * N[l] * detJ[j] * w[j];

			// assemble the global residual
			psolver->AssembleStiffness(se.m_node, lm, ke);
		}
	}
}

//-----------------------------------------------------------------------------
bool FERigidSphereContact::Augment(int naug)
{
	// make sure we need to augment
	if (!m_blaugon) return true;

	// let's stay positive
	bool bconv = true;

	// calculate initial norms
	double normL0 = 0;
	int c = 0;
	for (int i=0; i<m_ss.Elements(); ++i)
	{
		FESurfaceElement& el = m_ss.Element(i);
		for (int j=0; j<el.GaussPoints(); ++j)
		{
			FERigidSphereSurface::DATA& d = m_ss.m_data[c++];
			normL0 += d.Lm*d.Lm;
		}
	}
	normL0 = sqrt(normL0);

	// update Lagrange multipliers and calculate current norms
	double normL1 = 0;
	double normgc = 0;
	int N = 0;
	c = 0;
	for (int i = 0; i<m_ss.Elements(); ++i)
	{
		FESurfaceElement& el = m_ss.Element(i);
		for (int j = 0; j<el.GaussPoints(); ++j)
		{
			FERigidSphereSurface::DATA& d = m_ss.m_data[c++];
			
			// update Lagrange multipliers
			double Lm = d.Lm + m_eps*d.gap;
			Lm = MBRACKET(Lm);
			normL1 += Lm*Lm;
			if (d.gap > 0)
			{
				normgc += d.gap*d.gap;
				++N;
			}
		}	
	}
	if (N==0) N = 1;

	normL1 = sqrt(normL1);
	normgc = sqrt(normgc / N);

	// check convergence of constraints
	felog.printf(" rigid sphere contact # %d\n", GetID());
	felog.printf("                        CURRENT        REQUIRED\n");
	double pctn = 0;
	if (fabs(normL1) > 1e-10) pctn = fabs((normL1 - normL0)/normL1);
	felog.printf("    normal force : %15le %15le\n", pctn, m_atol);
	felog.printf("    gap function : %15le       ***\n", normgc);
		
	if (pctn >= m_atol)
	{
		bconv = false;
		c = 0;
		for (int i = 0; i<m_ss.Elements(); ++i)
		{
			FESurfaceElement& el = m_ss.Element(i);
			for (int j = 0; j<el.GaussPoints(); ++j)
			{
				FERigidSphereSurface::DATA& d = m_ss.m_data[c++];
	
				double Lm = d.Lm + m_eps*d.gap;
				d.Lm = MBRACKET(Lm);
			}	
		}
	}

	return bconv;
}

//-----------------------------------------------------------------------------

void FERigidSphereContact::Serialize(DumpStream &ar)
{
	FEContactInterface::Serialize(ar);
	m_ss.Serialize(ar);
	m_sphere.Serialize(ar);
}