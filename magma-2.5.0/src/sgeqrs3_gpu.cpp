/*
    -- MAGMA (version 2.5.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date January 2019

       @generated from src/zgeqrs3_gpu.cpp, normal z -> s, Sun Sep 15 17:13:39 2019

*/
#include "magma_internal.h"

/***************************************************************************//**
    Purpose
    -------
    SGEQRS solves the least squares problem
           min || A*X - C ||
    using the QR factorization A = Q*R computed by SGEQRF3_GPU.

    Arguments
    ---------
    @param[in]
    m       INTEGER
            The number of rows of the matrix A. M >= 0.

    @param[in]
    n       INTEGER
            The number of columns of the matrix A. M >= N >= 0.

    @param[in]
    nrhs    INTEGER
            The number of columns of the matrix C. NRHS >= 0.

    @param[in]
    dA      REAL array on the GPU, dimension (LDDA,N)
            The i-th column must contain the vector which defines the
            elementary reflector H(i), for i = 1,2,...,n, as returned by
            SGEQRF3_GPU in the first n columns of its array argument A.
            dA is modified by the routine but restored on exit.

    @param[in]
    ldda    INTEGER
            The leading dimension of the array A, LDDA >= M.

    @param[in]
    tau     REAL array, dimension (N)
            TAU(i) must contain the scalar factor of the elementary
            reflector H(i), as returned by MAGMA_SGEQRF_GPU.

    @param[in,out]
    dB      REAL array on the GPU, dimension (LDDB,NRHS)
            On entry, the M-by-NRHS matrix C.
            On exit, the N-by-NRHS solution matrix X.

    @param[in,out]
    dT      REAL array that is the output (the 6th argument)
            of magma_sgeqrf_gpu of size
            2*MIN(M, N)*NB + ceil(N/32)*32 )* MAX(NB, NRHS).
            The array starts with a block of size MIN(M,N)*NB that stores
            the triangular T matrices used in the QR factorization,
            followed by MIN(M,N)*NB block storing the diagonal block
            matrices for the R matrix, followed by work space of size
            (ceil(N/32)*32)* MAX(NB, NRHS).

    @param[in]
    lddb    INTEGER
            The leading dimension of the array dB. LDDB >= M.

    @param[out]
    hwork   (workspace) REAL array, dimension (LWORK)
            On exit, if INFO = 0, WORK[0] returns the optimal LWORK.

    @param[in]
    lwork   INTEGER
            The dimension of the array WORK,
            LWORK >= (M - N + NB)*(NRHS + NB) + NRHS*NB,
            where NB is the blocksize given by magma_get_sgeqrf_nb( M, N ).
    \n
            If LWORK = -1, then a workspace query is assumed; the routine
            only calculates the optimal size of the HWORK array, returns
            this value as the first entry of the WORK array.

    @param[out]
    info    INTEGER
      -     = 0:  successful exit
      -     < 0:  if INFO = -i, the i-th argument had an illegal value

    @ingroup magma_geqrs
*******************************************************************************/
extern "C" magma_int_t
magma_sgeqrs3_gpu(
    magma_int_t m, magma_int_t n, magma_int_t nrhs,
    magmaFloat_ptr dA,    magma_int_t ldda,
    float const *tau,
    magmaFloat_ptr dT,
    magmaFloat_ptr dB,    magma_int_t lddb,
    float *hwork, magma_int_t lwork,
    magma_int_t *info)
{
    #define dA(i_,j_) (dA + (i_) + (j_)*ldda)
    #define dT(i_)    (dT + (lddwork + (i_))*nb)

    float c_one     = MAGMA_S_ONE;
    magma_int_t min_mn, lddwork;

    magma_int_t nb     = magma_get_sgeqrf_nb( m, n );
    magma_int_t lwkopt = (m - n + nb)*(nrhs + nb) + nrhs*nb;
    bool lquery = (lwork == -1);

    hwork[0] = magma_smake_lwork( lwkopt );

    *info = 0;
    if (m < 0)
        *info = -1;
    else if (n < 0 || m < n)
        *info = -2;
    else if (nrhs < 0)
        *info = -3;
    else if (ldda < max(1,m))
        *info = -5;
    else if (lddb < max(1,m))
        *info = -8;
    else if (lwork < lwkopt && ! lquery)
        *info = -10;

    if (*info != 0) {
        magma_xerbla( __func__, -(*info) );
        return *info;
    }
    else if (lquery)
        return *info;

    min_mn = min(m,n);
    if (min_mn == 0) {
        hwork[0] = c_one;
        return *info;
    }
    lddwork = min_mn;

    magma_queue_t queue;
    magma_device_t cdev;
    magma_getdevice( &cdev );
    magma_queue_create( cdev, &queue );
    
    /* B := Q^H * B */
    magma_sormqr_gpu( MagmaLeft, MagmaTrans,
                      m, nrhs, n,
                      dA(0,0), ldda, tau,
                      dB, lddb, hwork, lwork, dT, nb, info );
    if ( *info != 0 ) {
        magma_queue_destroy( queue );
        return *info;
    }

    /* Solve R*X = B(1:n,:)
       1. Move the (min_mn - 1)/nb block diagonal submatrices from dT to R
       2. Solve
       3. Restore the data format moving data from R back to dT
    */
    magmablas_sswapdblk( min_mn-1, nb, dA(0,0), ldda, 1, dT(0), nb, 0, queue );
    if ( nrhs == 1 ) {
        magma_strsv( MagmaUpper, MagmaNoTrans, MagmaNonUnit, n,
                     dA(0,0), ldda,
                     dB,      1, queue );
    } else {
        magma_strsm( MagmaLeft, MagmaUpper, MagmaNoTrans, MagmaNonUnit, n, nrhs,
                     c_one, dA(0,0), ldda,
                            dB,      lddb, queue );
    }
    magmablas_sswapdblk( min_mn-1, nb, dT(0), nb, 0, dA(0,0), ldda, 1, queue );

    magma_queue_destroy( queue );
    return *info;
}
