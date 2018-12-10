/*
 *  PSA crypto layer on top of Mbed TLS crypto
 */
/*  Copyright (C) 2018, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_PSA_CRYPTO_C)

#include "psa/crypto.h"

#include "psa_crypto_core.h"
#include "psa_crypto_slot_management.h"
#include "psa_crypto_storage.h"

#include <stdlib.h>
#include <string.h>
#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#define mbedtls_calloc calloc
#define mbedtls_free   free
#endif

#define ARRAY_LENGTH( array ) ( sizeof( array ) / sizeof( *( array ) ) )

typedef struct
{
    psa_key_slot_t key_slots[PSA_KEY_SLOT_COUNT];
    unsigned key_slots_initialized : 1;
} psa_global_data_t;

psa_global_data_t global_data;

/* Access a key slot at the given handle. The handle of a key slot is
 * the index of the slot in the global slot array, plus one so that handles
 * start at 1 and not 0. */
psa_status_t psa_get_key_slot( psa_key_handle_t handle,
                               psa_key_slot_t **p_slot )
{
    psa_key_slot_t *slot = NULL;

    if( ! global_data.key_slots_initialized )
        return( PSA_ERROR_BAD_STATE );

    /* 0 is not a valid handle under any circumstance. This
     * implementation provides slots number 1 to N where N is the
     * number of available slots. */
    if( handle == 0 || handle > ARRAY_LENGTH( global_data.key_slots ) )
        return( PSA_ERROR_INVALID_HANDLE );
    slot = &global_data.key_slots[handle - 1];

    /* If the slot hasn't been allocated, the handle is invalid. */
    if( ! slot->allocated )
        return( PSA_ERROR_INVALID_HANDLE );

    *p_slot = slot;
    return( PSA_SUCCESS );
}

psa_status_t psa_initialize_key_slots( void )
{
    /* Nothing to do: program startup and psa_wipe_all_key_slots() both
     * guarantee that the key slots are initialized to all-zero, which
     * means that all the key slots are in a valid, empty state. */
    global_data.key_slots_initialized = 1;
    return( PSA_SUCCESS );
}

void psa_wipe_all_key_slots( void )
{
    psa_key_handle_t key;
    for( key = 1; key <= PSA_KEY_SLOT_COUNT; key++ )
    {
        psa_key_slot_t *slot = &global_data.key_slots[key - 1];
        (void) psa_wipe_key_slot( slot );
    }
    global_data.key_slots_initialized = 0;
}

/** Find a free key slot and mark it as in use.
 *
 * \param[out] handle   On success, a slot number that is not in use.
 *
 * \retval #PSA_SUCCESS
 * \retval #PSA_ERROR_INSUFFICIENT_MEMORY
 */
static psa_status_t psa_internal_allocate_key_slot( psa_key_handle_t *handle )
{
    for( *handle = PSA_KEY_SLOT_COUNT; *handle != 0; --( *handle ) )
    {
        psa_key_slot_t *slot = &global_data.key_slots[*handle - 1];
        if( ! slot->allocated )
        {
            slot->allocated = 1;
            return( PSA_SUCCESS );
        }
    }
    return( PSA_ERROR_INSUFFICIENT_MEMORY );
}

psa_status_t psa_allocate_key( psa_key_type_t type,
                               size_t max_bits,
                               psa_key_handle_t *handle )
{
    /* This implementation doesn't reserve memory for the keys. */
    (void) type;
    (void) max_bits;
    *handle = 0;
    return( psa_internal_allocate_key_slot( handle ) );
}

static psa_status_t persistent_key_setup( psa_key_lifetime_t lifetime,
                                          psa_key_id_t id,
                                          psa_key_handle_t *handle,
                                          psa_status_t wanted_load_status )
{
    psa_status_t status;

    *handle = 0;

    if( lifetime != PSA_KEY_LIFETIME_PERSISTENT )
        return( PSA_ERROR_INVALID_ARGUMENT );

    status = psa_internal_allocate_key_slot( handle );
    if( status != PSA_SUCCESS )
        return( status );

    status = psa_internal_make_key_persistent( *handle, id );
    if( status != wanted_load_status )
    {
        psa_internal_release_key_slot( *handle );
        *handle = 0;
    }
    return( status );
}

psa_status_t psa_open_key( psa_key_lifetime_t lifetime,
                           psa_key_id_t id,
                           psa_key_handle_t *handle )
{
    return( persistent_key_setup( lifetime, id, handle, PSA_SUCCESS ) );
}

psa_status_t psa_create_key( psa_key_lifetime_t lifetime,
                             psa_key_id_t id,
                             psa_key_type_t type,
                             size_t max_bits,
                             psa_key_handle_t *handle )
{
    psa_status_t status;

    /* This implementation doesn't reserve memory for the keys. */
    (void) type;
    (void) max_bits;

    status = persistent_key_setup( lifetime, id, handle,
                                   PSA_ERROR_EMPTY_SLOT );
    switch( status )
    {
        case PSA_SUCCESS: return( PSA_ERROR_OCCUPIED_SLOT );
        case PSA_ERROR_EMPTY_SLOT: return( PSA_SUCCESS );
        default: return( status );
    }
}

psa_status_t psa_close_key( psa_key_handle_t handle )
{
    return( psa_internal_release_key_slot( handle ) );
}

#endif /* MBEDTLS_PSA_CRYPTO_C */
