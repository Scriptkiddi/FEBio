#include "stdafx.h"
#include "FESurfaceToSurfaceMap.h"
#include "FEModel.h"
#include "FEMesh.h"
#include "FESurface.h"

BEGIN_FECORE_CLASS(FESurfaceToSurfaceMap, FEDataGenerator)
	ADD_PARAMETER(m_surfName1, "bottom_surface");
	ADD_PARAMETER(m_surfName2, "top_surface");

	ADD_PROPERTY(m_func, "function");
END_FECORE_CLASS();

FESurfaceToSurfaceMap::FESurfaceToSurfaceMap(FEModel* fem) : FEDataGenerator(fem)
{
	m_ccp = 0;
	m_npr = 0;
	m_func = 0;
}

FESurfaceToSurfaceMap::~FESurfaceToSurfaceMap()
{
	if (m_ccp) delete m_ccp;
	if (m_npr) delete m_npr;
}

bool FESurfaceToSurfaceMap::Init()
{
	FEModel* fem = GetFEModel();
	if (fem == 0) return false;

	if (m_func == 0) return false;

	FEMesh& mesh = fem->GetMesh();

	FEFacetSet* fs1 = mesh.FindFacetSet(m_surfName1);
	FEFacetSet* fs2 = mesh.FindFacetSet(m_surfName2);
	if ((fs1 == 0) || (fs2 == 0)) return false;

	m_surf1 = new FESurface(fem);
	m_surf1->BuildFromSet(*fs1);

	m_surf2 = new FESurface(fem);
	m_surf2->BuildFromSet(*fs2);

	// we need to invert the second surface, otherwise the normal projections won't work
	m_surf2->Invert();

	m_ccp = new FEClosestPointProjection(*m_surf1);
	if (m_ccp->Init() == false) return false;

	double R = mesh.GetBoundingBox().radius();
	m_npr = new FENormalProjection(*m_surf2);
	m_npr->SetSearchRadius(R);
	m_npr->SetTolerance(0.001);
	m_npr->Init();

	return true;
}

void FESurfaceToSurfaceMap::value(const vec3d& x, double& data)
{
	vec3d r(x);

	// project x onto surface 1 using CCP
	vec3d q1(0,0,0);
	vec2d r1;
	m_ccp->Project(r, q1, r1);

	// project x onto surface 2 using ray-intersection
	vec3d N = r - q1; N.unit();
	vec3d q2 = m_npr->Project(q1, N);
	double L2 = (q2 - q1)*(q2 - q1);
	if (L2 == 0.0) L2 = 1.0;

	// find the fractional distance
	double w = ((x - q1)*(q2 - q1))/L2;

	// evaluate the function
	data = m_func->value(w);
}