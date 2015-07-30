#include "stdafx.h"
#include "BlockMatrix.h"
#include <assert.h>

//-----------------------------------------------------------------------------
BlockMatrix::BlockMatrix()
{
}

//-----------------------------------------------------------------------------
BlockMatrix::~BlockMatrix()
{
	// clear memory allocations
	Clear();

	// delete blocks
	const int n = (int) m_Block.size();
	for (int i=0; i<n; ++i) delete m_Block[i].pA;
}

//-----------------------------------------------------------------------------
//! This function sets the partitions for the blocks. 
//! The partition list contains the number of rows (and cols) for each partition.
//! So for instance, if part = {10,10}, a 2x2=4 partition is created, where
//! each block is a 10x10 matrix.
//
//! TODO: I want to put the partition information in the matrix profile structure
//!       so that the Create function can be used to create all the blocks.
void BlockMatrix::Partition(const vector<int>& part)
{
	// copy the partitions, but store equation numbers instead of number of equations
	const int n = part.size();
	m_part.resize(n+1);
	m_part[0] = 0;
	for (int i=0; i<n; ++i) m_part[i+1] = m_part[i] + part[i];

	// create the block structure for all the partitions
	m_Block.resize(n*n);
	int nrow = 0;
	for (int i=0; i<n; ++i) // loop over rows
	{
		int ncol = 0;
		for (int j=0; j<n; ++j) // loop over cols
		{
			BLOCK& Bij = m_Block[i*n+j];
			Bij.nstart_row = nrow;
			Bij.nend_row   = Bij.nstart_row + part[i] - 1;

			Bij.nstart_col = ncol;
			Bij.nend_col   = Bij.nstart_col + part[j] - 1;

			// Note the parameters in the constructors.
			// This is because we are using Pardiso for this
			if (i==j)
				Bij.pA = new CompactSymmMatrix(1);
			else
				Bij.pA = new CompactUnSymmMatrix(1, true);
			
			ncol += part[j];
		}
		nrow += part[i];
	}
}

//-----------------------------------------------------------------------------
//! Create a sparse matrix from a sparse-matrix profile
void BlockMatrix::Create(SparseMatrixProfile& MP)
{
	m_ndim = MP.size();
	m_nsize = 0;
	const int N = (int) m_Block.size();
	for (int i=0; i<N; ++i)
	{
		BLOCK& Bi = m_Block[i];
		SparseMatrixProfile MPi = MP.GetBlockProfile(Bi.nstart_row, Bi.nstart_col, Bi.nend_row, Bi.nend_col);
		Bi.pA->Create(MPi);
		m_nsize += Bi.pA->NonZeroes();
	}
}

//-----------------------------------------------------------------------------
//! assemble a matrix into the sparse matrix
void BlockMatrix::Assemble(matrix& ke, std::vector<int>& lm)
{
	int I, J;
	const int N = ke.rows();
	for (int i=0; i<N; ++i)
	{
		if ((I = lm[i])>=0)
		{
			for (int j=0; j<N; ++j)
			{
				if ((J = lm[j]) >= 0) add(I,J, ke[i][j]);
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! assemble a matrix into the sparse matrix
void BlockMatrix::Assemble(matrix& ke, std::vector<int>& lmi, std::vector<int>& lmj)
{
	int I, J;
	const int N = ke.rows();
	const int M = ke.columns();
	for (int i=0; i<N; ++i)
	{
		if ((I = lmi[i])>=0)
		{
			for (int j=0; j<M; ++j)
			{
				if ((J = lmj[j]) >= 0) add(I,J, ke[i][j]);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// helper function for finding partitions
int BlockMatrix::find_partition(int i)
{
	const int N = m_part.size() - 1;
	int n = 0;
	for (; n<N; ++n)
		if (m_part[n+1] > i) break;
	assert(n<N);
	return n;
}

//-----------------------------------------------------------------------------
// helper function for finding a block
BlockMatrix::BLOCK& BlockMatrix::Block(int i, int j)
{
	const int N = m_part.size() - 1;
	return m_Block[i*N+j];
}

//-----------------------------------------------------------------------------
//! set entry to value
void BlockMatrix::set(int i, int j, double v)
{
	int nr = find_partition(i);
	int nc = find_partition(j);
	Block(nr, nc).pA->set(i - m_part[nr], i - m_part[nc], v);
}

//-----------------------------------------------------------------------------
//! add value to entry
void BlockMatrix::add(int i, int j, double v)
{
	int nr = find_partition(i);
	int nc = find_partition(j);
	Block(nr, nc).pA->add(i - m_part[nr], i - m_part[nc], v);
}

//-----------------------------------------------------------------------------
//! retrieve value
double BlockMatrix::get(int i, int j)
{
	int nr = find_partition(i);
	int nc = find_partition(j);
	return Block(nr, nc).pA->get(i - m_part[nr], i - m_part[nc]);
}

//-----------------------------------------------------------------------------
//! get the diagonal value
double BlockMatrix::diag(int i)
{
	int n = find_partition(i);
	return Block(n, n).pA->diag(i - m_part[n]);
}

//-----------------------------------------------------------------------------
//! release memory for storing data
void BlockMatrix::Clear()
{
	// clear the blocks
	const int n = (int) m_Block.size();
	for (int i=0; i<n; ++i) m_Block[i].pA->Clear();
}

//-----------------------------------------------------------------------------
//! Zero all matrix elements
void BlockMatrix::zero()
{
	// zero the blocks
	const int n = (int) m_Block.size();
	for (int i=0; i<n; ++i) m_Block[i].pA->zero();
}