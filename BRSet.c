//
//  BRSet.c
//
//  Created by Aaron Voisine on 9/11/15.
//  Copyright (c) 2015 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "BRSet.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <hashmap.h>
#include <math.h>

struct BRSetStruct {
    map_t **map; // map of item has to position
    size_t (*hash)(const void *); // hash function
    int (*eq)(const void *, const void *); // equality function
};

static void _BRSetInit(BRSet *set, size_t (*hash)(const void *), int (*eq)(const void *, const void *), size_t capacity)
{
    assert(set != NULL);
    assert(hash != NULL);
    assert(eq != NULL);
    assert(capacity >= 0);

    set->map = hashmap_new();
    set->hash = hash;
    set->eq = eq;
}

// retruns a newly allocated empty set that must be freed by calling BRSetFree()
// size_t hash(const void *) is a function that returns a hash value for a given set item
// int eq(const void *, const void *) is a function that returns true if two set items are equal
// any two items that are equal must also have identical hash values
// capacity is the maximum estimated number of items the set will need to hold
BRSet *BRSetNew(size_t (*hash)(const void *), int (*eq)(const void *, const void *), size_t capacity)
{
    BRSet *set = calloc(1, sizeof(*set));

    assert(set != NULL);
    _BRSetInit(set, hash, eq, capacity);
    return set;
}

// adds given item to set or replaces an equivalent existing item and returns item replaced if any
void *BRSetAdd(BRSet *set, void *item)
{
    assert(set != NULL);
    assert(item != NULL);

    size_t hash = set->hash(item);
    void *t = hashmap_get(set->map, hash);
    hashmap_put(set->map, hash, item);
    return t;
}

// removes item equivalent to given item from set and returns item removed if any
void *BRSetRemove(BRSet *set, const void *item)
{
    assert(set != NULL);
    assert(item != NULL);

    size_t hash = set->hash(item);
    void *r = hashmap_get(set->map, hash);
    hashmap_remove(set->map, hash);
    return r;
}

// removes all items from set
void BRSetClear(BRSet *set)
{
    assert(set != NULL);
    hashmap_clear(set->map);
}

// returns the number of items in set
size_t BRSetCount(const BRSet *set)
{
    assert(set != NULL);

    return hashmap_length(set->map);
}

// true if an item equivalant to the given item is contained in set
int BRSetContains(const BRSet *set, const void *item)
{
    return (BRSetGet(set, item) != NULL);
}

// returns member item from set equivalent to given item, or NULL if there is none
void *BRSetGet(const BRSet *set, const void *item)
{
    assert(set != NULL);
    assert(item != NULL);

    size_t hash = set->hash(item);
    size_t *t = hashmap_get(set->map, hash);
    return t;
}

// calls apply() with each item in set
void BRSetApply(const BRSet *set, void *info, void (*apply)(void *info, void *item))
{
    assert(set != NULL);
    assert(apply != NULL);

    size_t i = 0, size = hashmap_length(set->map);
    void *t;
    
    while (i < size) {
        t = hashmap_get_index(set->map, i++);
        if (t) apply(info, t);
    }
}

// frees memory allocated for set
void BRSetFree(BRSet *set)
{
    assert(set != NULL);
    hashmap_free(set->map);
    free(set);
}