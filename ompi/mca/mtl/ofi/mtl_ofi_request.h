/*
 * Copyright (c) 2013-2016 Intel, Inc. All rights reserved
 * Copyright (c) 2017      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2025      Amazon.com, Inc. or its affiliates.  All Rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 * SPDX-License-Identifier: BSD-3-Clause-Open-MPI
 */

#ifndef OMPI_MTL_OFI_REQUEST_H
#define OMPI_MTL_OFI_REQUEST_H

#include "mtl_ofi.h"

#define TO_OFI_REQ(_ptr_ctx) \
    container_of((_ptr_ctx), struct ompi_mtl_ofi_request_t, ctx)

typedef enum {
    OMPI_MTL_OFI_SEND,
    OMPI_MTL_OFI_RECV,
    OMPI_MTL_OFI_ACK,
    OMPI_MTL_OFI_PROBE
} ompi_mtl_ofi_request_type_t;

struct ompi_mtl_ofi_reg_t;
struct ompi_mtl_ofi_request_t;

struct ompi_mtl_ofi_request_t {
    struct mca_mtl_request_t super;

    /** OFI Request type */
    ompi_mtl_ofi_request_type_t type;

    /** OFI context */
    struct fi_context2 ctx;

    /** Completion count used by blocking and/or synchronous operations */
    volatile int completion_count;

    /** For a chunk of a striped message, the request the chunk belongs to.
     *  NULL on every other request. Distinct from parent below, which chains
     *  a synchronous send to its ack. */
    struct ompi_mtl_ofi_request_t *stripe_parent;

    /** For a striped message, chunk requests to release on completion. */
    struct ompi_mtl_ofi_request_t *chunks;

    /** Chunks of a striped message still in flight. The message completes when
     *  this reaches zero, not when any single chunk does. */
    volatile int chunks_outstanding;

    /** Which chunk of a striped message this request carries. */
    int stripe_index;

    /** How many chunks this side split the transfer into. */
    int chunks_expected;

    /** Set on a chunk whose operation was cancelled. fi_cancel is asynchronous and
     *  the operation still reports to the completion queue, so its callback must
     *  not charge the message a second time. */
    bool stripe_cancelled;

    /** Completion entry from chunk 0, which is the one carrying the tag and
     *  source the caller has to be told about. */
    struct fi_cq_tagged_entry stripe_wc;

    /** Event callback */
    int (*event_callback)(struct fi_cq_tagged_entry *wc,
                          struct ompi_mtl_ofi_request_t*);

    /** Error callback */
    int (*error_callback)(struct fi_cq_err_entry *error,
                          struct ompi_mtl_ofi_request_t*);

    /** Request status */
    struct ompi_status_public_t status;

    /** Match state used by Probe */
    int match_state;

    /** Reference to the communicator used to  */
    /*  lookup source of an ANY_SOURCE Recv    */
    struct ompi_communicator_t *comm;

    /** Reference to the MTL used to lookup */
    /*  source of an ANY_SOURCE Recv        */
    struct mca_mtl_base_module_t* mtl;

    /** Pack buffer */
    void *buffer;

    /** Pack buffer size */
    size_t length;

    /** Pack buffer convertor */
    struct opal_convertor_t *convertor;

    /** Flag to prevent MPI_Cancel from cancelling a started Recv request */
    volatile bool req_started;

    /** Request's tag used in case of an error. Also for FI_CLAIM requests. */
    uint64_t match_bits;

    /** Used to build msg for fi_trecvmsg with FI_CLAIM  */
    uint64_t mask_bits;

    /** Remote OFI address used when a Recv needs to be ACKed */
    fi_addr_t remote_addr;

    /** Parent request which needs to be ACKed (e.g. Synchronous Send) */
    struct ompi_mtl_ofi_request_t *parent;

    /** Pointer to Mrecv request to complete */
    struct mca_mtl_request_t *mrecv_req;

    /** Stores reference to memory region from registration */

    /*  Set to NULL if memory not registered */
    struct ompi_mtl_ofi_reg_t *mr;
};
typedef struct ompi_mtl_ofi_request_t ompi_mtl_ofi_request_t;

#endif
