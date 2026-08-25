/*
 * Copyright (c) 2026      Amazon.com, Inc. or its affiliates.  All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

/*
 * Exercise the osc/rdma window-state atomics on the configuration that
 * carries them: a window spanning more than one node, with more than one
 * process per node, over a BTL that has no hardware atomics.
 *
 * osc/rdma decides per peer whether window state may be updated with CPU
 * atomics or has to go through the BTL.  That decision only has more than
 * one possible answer when a window has node-local peers *and* off-node
 * peers, so a single-node run takes a short-circuit and a
 * one-process-per-node run has no node-local peers at all.  Neither
 * exercises the decision.  Faults on this path have also been observed to
 * need four or more processes per node before they appear at all.
 *
 * Run it with an alternate (non-accelerated) BTL so the atomics are the
 * emulated, active-message kind, for example
 *
 *   mpirun -np 8 -N 4 --mca pml ob1 --mca btl tcp,self \
 *       ./osc_rdma_shared_state_atomics
 *
 * The program reports the layout it actually got and says so when that
 * layout cannot cover the case, rather than passing quietly.
 *
 * Exits 0 when every phase validates, 1 otherwise.
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define ITERS 200

int main(int argc, char **argv)
{
    int rank, size, i, errs = 0, local_size, local_rank, nodes;
    long *base = NULL;
    long value, result, expected;
    MPI_Win win;
    MPI_Comm shared;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* Report the layout, and whether it can cover the intended case. */
    MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL,
                        &shared);
    MPI_Comm_size(shared, &local_size);
    MPI_Comm_rank(shared, &local_rank);
    nodes = (0 == local_rank) ? 1 : 0;
    MPI_Allreduce(MPI_IN_PLACE, &nodes, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (0 == rank) {
        printf("layout: %d processes, %d nodes, %d processes on this node\n",
               size, nodes, local_size);
        if (nodes < 2) {
            printf("NOTE: one node only - osc/rdma takes the single-node "
                   "short-circuit, so this run does not cover the shared "
                   "state decision\n");
        } else if (local_size < 4) {
            printf("NOTE: %d processes per node - faults on this path have "
                   "needed 4 or more, so this run is weaker than intended\n",
                   local_size);
        }
        fflush(stdout);
    }

    MPI_Win_allocate(sizeof(long), sizeof(long), MPI_INFO_NULL, MPI_COMM_WORLD,
                     &base, &win);

    /*
     * Phase 1: fetch-and-op under a shared lock epoch.  Every process adds
     * one to its right-hand neighbour on every iteration, so each window
     * receives exactly ITERS increments from exactly one process.
     */
    *base = 0;
    MPI_Barrier(MPI_COMM_WORLD);

    value = 1;
    for (i = 0; i < ITERS; i++) {
        MPI_Win_lock_all(0, win);
        MPI_Fetch_and_op(&value, &result, MPI_LONG, (rank + 1) % size, 0,
                         MPI_SUM, win);
        MPI_Win_flush_all(win);
        MPI_Win_unlock_all(win);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    if (*base != (long) ITERS) {
        printf("rank %d: fetch_and_op phase: window holds %ld, expected %d\n",
               rank, *base, ITERS);
        errs++;
    }

    /*
     * Phase 2: accumulate with a passive-target exclusive lock, which takes
     * the window-state lock path rather than the data path.
     */
    *base = 0;
    MPI_Barrier(MPI_COMM_WORLD);

    value = 2;
    for (i = 0; i < ITERS; i++) {
        int target = (rank + 1) % size;
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target, 0, win);
        MPI_Accumulate(&value, 1, MPI_LONG, target, 0, 1, MPI_LONG, MPI_SUM,
                       win);
        MPI_Win_unlock(target, win);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    expected = 2 * (long) ITERS;
    if (*base != expected) {
        printf("rank %d: accumulate phase: window holds %ld, expected %ld\n",
               rank, *base, expected);
        errs++;
    }

    /*
     * Phase 3: compare-and-swap.  Each process walks its own window from 0
     * to ITERS with a compare-and-swap per step, so a dropped or misrouted
     * response shows up as a stalled or wrong counter rather than a hang in
     * one particular rank.
     */
    *base = 0;
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Win_lock_all(0, win);
    for (i = 0; i < ITERS; i++) {
        long want = (long) i;
        long next = (long) i + 1;

        MPI_Compare_and_swap(&next, &want, &result, MPI_LONG, rank, 0, win);
        MPI_Win_flush(rank, win);
        if (result != want) {
            printf("rank %d: compare_and_swap step %d: saw %ld, expected %ld\n",
                   rank, i, result, want);
            errs++;
            break;
        }
    }
    MPI_Win_unlock_all(win);
    MPI_Barrier(MPI_COMM_WORLD);

    if (0 == errs && *base != (long) ITERS) {
        printf("rank %d: compare_and_swap phase: window holds %ld, expected %d\n",
               rank, *base, ITERS);
        errs++;
    }

    MPI_Win_free(&win);
    MPI_Comm_free(&shared);

    MPI_Allreduce(MPI_IN_PLACE, &errs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (0 == rank) {
        printf("%s: %d error(s)\n", (0 == errs) ? "PASS" : "FAIL", errs);
        fflush(stdout);
    }

    MPI_Finalize();
    return (0 == errs) ? 0 : 1;
}
