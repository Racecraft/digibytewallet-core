/*
 * Generic map implementation.
 */
#include "hashmap.h"

#include <stdlib.h>
#include <stdio.h>

#define INITIAL_SIZE (256)
#define MAX_CHAIN_LENGTH (8)

/* We need to keep keys and values */
typedef struct _hashmap_element{
    size_t key;
    int in_use;
    any_t data;
} hashmap_element;

/* A hashmap has some maximum size and current size,
 * as well as the data to hold. */
typedef struct _hashmap_map{
    int table_size;
    int size;
    hashmap_element *data;
} hashmap_map;

/*
 * Return an empty hashmap, or NULL on failure.
 */
map_t hashmap_new() {
    hashmap_map* m = (hashmap_map*) malloc(sizeof(hashmap_map));
    if(!m) goto err;

    m->data = (hashmap_element*) calloc(INITIAL_SIZE, sizeof(hashmap_element));
    if(!m->data) goto err;

    m->table_size = INITIAL_SIZE;
    m->size = 0;

    return m;
    err:
    if (m)
        hashmap_free(m);
    return NULL;
}

/* The implementation here was originally done by Gary S. Brown. It has been substantially
 * simplified to usean any_t as the inKey instead of a char* //ylo */

/* ============================================================= */
/*  COPYRIGHT (C) 1986 Gary S. Brown.  You may use this program, or       */
/*  code or tables extracted from it, as desired without restriction.     */
/*  --------------------------------------------------------------------  */

/*
 * Hashing function for a string
 */
unsigned int hashmap_hash_int(hashmap_map * m, any_t inKey){

    unsigned int key = inKey;

    /* Robert Jenkins' 32 bit Mix Function */
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    /* Knuth's Multiplicative Method */
    key = (key >> 3) * 2654435761;

    return key % m->table_size;
}

/*
 * Return the integer of the location in data
 * to store the point to the item, or MAP_FULL.
 */
int hashmap_hash(map_t in, any_t key){
    int curr;
    int i;

    /* Cast the hashmap */
    hashmap_map* m = (hashmap_map *) in;

    /* If full, return immediately */
    if(m->size >= (m->table_size/2)) return MAP_FULL;

    /* Find the best index */
    curr = hashmap_hash_int(m, key);

    /* Linear probing */
    for(i = 0; i< MAX_CHAIN_LENGTH; i++){
        if(m->data[curr].in_use == 0)
            return curr;

        if(m->data[curr].in_use == 1 && (m->data[curr].key == key))
            return curr;

        curr = (curr + 1) % m->table_size;
    }

    return MAP_FULL;
}

/*
 * Doubles the size of the hashmap, and rehashes all the elements
 */
int hashmap_rehash(map_t in){
    int i;
    int old_size;
    hashmap_element* curr;

    /* Setup the new elements */
    hashmap_map *m = (hashmap_map *) in;
    hashmap_element* temp = (hashmap_element *)
            calloc(2 * m->table_size, sizeof(hashmap_element));
    if(!temp) return MAP_OMEM;

    /* Update the array */
    curr = m->data;
    m->data = temp;

    /* Update the size */
    old_size = m->table_size;
    m->table_size = 2 * m->table_size;
    m->size = 0;

    /* Rehash the elements */
    for(i = 0; i < old_size; i++){
        int status;

        if (curr[i].in_use == 0)
            continue;

        status = hashmap_put(m, curr[i].key, curr[i].data);
        if (status != MAP_OK)
            return status;
    }

    free(curr);

    return MAP_OK;
}

/*
 * Add a pointer to the hashmap with some key
 */
int hashmap_put(map_t in, any_t key, any_t value){
    int index;
    hashmap_map* m;

    /* Cast the hashmap */
    m = (hashmap_map *) in;

    /* Find a place to put our value */
    index = hashmap_hash(in, key);
    while(index == MAP_FULL){
        if (hashmap_rehash(in) == MAP_OMEM) {
            return MAP_OMEM;
        }
        index = hashmap_hash(in, key);
    }

    /* Set the data */
    m->data[index].data = value;
    m->data[index].key = key;
    m->data[index].in_use = 1;
    m->size++;

    return MAP_OK;
}

/*
 * Get your pointer out of the hashmap with a key
 */
any_t hashmap_get(map_t in, any_t key){
    int curr;
    int i;
    hashmap_map* m;

    /* Cast the hashmap */
    m = (hashmap_map *) in;

    /* Find data location */
    curr = hashmap_hash_int(m, key);

    /* Linear probing, if necessary */
    for(i = 0; i<MAX_CHAIN_LENGTH; i++){

        int in_use = m->data[curr].in_use;
        if (in_use == 1){
            if (m->data[curr].key == key){
                return (m->data[curr].data);
            }
        }

        curr = (curr + 1) % m->table_size;
    }

    /* Not found */
    return NULL;
}

/*
 * Remove an element with that key from the map
 */
int hashmap_remove(map_t in, any_t key){
    int i;
    int curr;
    hashmap_map* m;

    /* Cast the hashmap */
    m = (hashmap_map *) in;

    /* Find key */
    curr = hashmap_hash_int(m, key);

    /* Linear probing, if necessary */
    for(i = 0; i<MAX_CHAIN_LENGTH; i++){

        int in_use = m->data[curr].in_use;
        if (in_use == 1){
            if (m->data[curr].key == key){
                /* Blank out the fields */
                m->data[curr].in_use = 0;
                m->data[curr].data = NULL;
                m->data[curr].key = NULL;

                /* Reduce the size */
                m->size--;
                return MAP_OK;
            }
        }
        curr = (curr + 1) % m->table_size;
    }

    /* Data not found */
    return MAP_MISSING;
}

/*
 * Remove an element with that key from the map
 */
int hashmap_clear(map_t in){
    int i;
    hashmap_map* m;

    /* Cast the hashmap */
    m = (hashmap_map *) in;

    /* Linear probing, if necessary */
    for(i = 0; i<MAX_CHAIN_LENGTH; i++){
        /* Blank out the fields */
        m->data[i].in_use = 0;
        m->data[i].data = NULL;
        m->data[i].key = NULL;

        /* Reduce the size */
        m->size--;
        return MAP_OK;
    }

    /* Data not found */
    return MAP_MISSING;
}

any_t hashmap_get_index(map_t in, int index) {
    hashmap_map* m;

    /* Cast the hashmap */
    m = (hashmap_map *) in;
    return (m->data[index].data);
}

/* Deallocate the hashmap */
void hashmap_free(map_t in){
    hashmap_map* m = (hashmap_map*) in;
    free(m->data);
    free(m);
}

/* Return the length of the hashmap */
int hashmap_length(map_t in){
    hashmap_map* m = (hashmap_map *) in;
    if(m != NULL) return m->size;
    else return 0;
}