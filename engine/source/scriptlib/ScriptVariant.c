/*
 * OpenBOR - http://www.chronocrash.com
 * -----------------------------------------------------------------------
 * All rights reserved, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c)  OpenBOR Team
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include "globals.h"
#include "ScriptVariant.h"

#define STRCACHE_INC      64

/*
* Caskey, Damon V.
* 2023-04-17
* 
* Populate a list of metadata for
* each variant type.
*/
const s_script_variant_meta script_variant_meta_list[] = {
    [VT_EMPTY] = {
        .id_string = "VT_EMPTY",
        .print_format = "%s",
    },

    [VT_INTEGER] = {
        .id_string = "VT_INTEGER",
        .print_format = "%ld",
    },

    [VT_DECIMAL] = {
        .id_string = "VT_DECIMAL",
        .print_format = "%f",
    },

    [VT_INTEGER64] = {
        .id_string = "VT_INTEGER64",
        .print_format = "%" PRId64,
    },

    [VT_UINTEGER64] = {
        .id_string = "VT_UINTEGER64",
        .print_format = "%" PRIu64,
    },

    [VT_PTR] = {
        .id_string = "VT_PTR",
        .print_format = "%p",
    },

    [VT_STR] = {
        .id_string = "VT_STR",
        .print_format = "%s",
    },
};

typedef struct
{
    int len;
    int ref;
    CHAR *str;
} Varstr;

// use string cache to cut the memory usage down, because not all variants are string, no need to give each of them an array
Varstr *strcache = NULL;
int   strcache_size = 0;
int   strcache_top = -1;
int  *strcache_index = NULL;

//clear the string cache
void StrCache_Clear()
{
    int i;
    if(strcache)
    {
        for(i = 0; i < strcache_size; i++)
        {
            if(strcache[i].str)
            {
                free(strcache[i].str);
            }
            strcache[i].str = NULL;
        }
        free(strcache);
        strcache = NULL;
    }
    if(strcache_index)
    {
        free(strcache_index);
        strcache_index = NULL;
    }
    strcache_size = 0;
    strcache_top = -1;
}

// init the string cache
void StrCache_Init()
{
    int i;
    StrCache_Clear(); // just in case
    strcache = calloc(STRCACHE_INC, sizeof(*strcache));
    strcache_index = malloc(sizeof(*strcache_index) * STRCACHE_INC);
    for(i = 0; i < STRCACHE_INC; i++)
    {
        strcache[i].str = NULL;
        strcache_index[i] = i;
    }
    strcache_size = STRCACHE_INC;
    strcache_top = strcache_size - 1;
}

// unrefs a string
void StrCache_Collect(int index)
{
    assert(index >= 0);
    assert(strcache[index].ref > 0);

    strcache[index].ref--;
    if(!strcache[index].ref)
    {
        //assert(strcache_top+1<strcache_size);
        free(strcache[index].str);
        strcache[index].str = NULL;
        strcache_index[++strcache_top] = index;
    }
}

int StrCache_Pop(int length)
{
    int i;
    if(strcache_size == 0)
    {
        StrCache_Init();
    }
    if(strcache_top < 0) // realloc
    {
        __reallocto(strcache, strcache_size, strcache_size + STRCACHE_INC);
        __reallocto(strcache_index, strcache_size, strcache_size + STRCACHE_INC);
        for(i = 0; i < STRCACHE_INC; i++)
        {
            strcache_index[i] = strcache_size + i;
        }

        memset(strcache + strcache_size, 0, sizeof(*strcache) * STRCACHE_INC);

        //printf("debug: dumping string cache....\n");
        //for(i=0; i<strcache_size; i++)
        //	printf("\t\"%s\"  %d\n", strcache[i].str, strcache[i].ref);

        strcache_size += STRCACHE_INC;
        strcache_top += STRCACHE_INC;

        //printf("debug: string cache resized to %d \n", strcache_size);
    }
    i = strcache_index[strcache_top--];
    strcache[i].str = malloc(length + 1);
    strcache[i].str[0] = 0;
    strcache[i].len = length;
    strcache[i].ref = 1;
    return i;
}

int StrCache_CreateNewFrom(const CHAR *str)
{
    int len = strlen(str);
    int strVal = StrCache_Pop(len);
    memcpy(StrCache_Get(strVal), str, len + 1);
    return strVal;
}

CHAR *StrCache_Get(int index)
{
    assert(index >= 0);
    //assert(index<strcache_size);
    return strcache[index].str;
}

int StrCache_Len(int index)
{
    assert(index >= 0);
    //assert(index<strcache_size);
    return strcache[index].len;
}

// increments the refcount of a string
void StrCache_Grab(int index)
{
    assert(index >= 0);
    assert(strcache[index].ref > 0);
    ++strcache[index].ref;
}


void ScriptVariant_Clear(ScriptVariant *var)
{
    ScriptVariant_ChangeType(var, VT_EMPTY);
    var->ptrVal = NULL; // not sure, maybe this is the longest member in the union
}

void ScriptVariant_Init(ScriptVariant *var)
{
    //memset(var, 0, 8);
    var->ptrVal = NULL; // not sure, maybe this is the longest member in the union
    var->vt = VT_EMPTY;
}

void ScriptVariant_ChangeType(ScriptVariant *var, VARTYPE cvt)
{
    // Always collect make it safer for string copy
    // since now reference has been added.
    // String variables should never be changed
    // unless the engine is creating a new one
    if(var->vt == VT_STR)
    {
        StrCache_Collect(var->strVal);
    }
    var->vt = cvt;
    if (cvt == VT_STR)
    {
        var->strVal = -1;
    }
}

// find an existing constant before copy
void ScriptVariant_ParseStringConstant(ScriptVariant *var, CHAR *str)
{
    //assert(index<strcache_size);
    //assert(size>0);
    int i;
    for(i = 0; i < strcache_size; i++)
    {
        if (strcache[i].ref && strcmp(str, strcache[i].str) == 0)
        {
            var->strVal = i;
            strcache[i].ref++;
            var->vt = VT_STR;
            return;
        }
    }

    ScriptVariant_ChangeType(var, VT_STR);
    var->strVal = StrCache_CreateNewFrom(str);
}

/*
* Caskey, Damon V.
* Orginal author (Utunels?) and date unknown.
* 
* Reworked 2026-06-02 to safely fail on overflow
* so script can support 64-bit integers.
*
* Get a 32-bit integer value from a variant. 
* Will attempt to convert if possible. 
*/
HRESULT ScriptVariant_IntegerValue(ScriptVariant *var, LONG *pVal) {

    int64_t temp;

    if (!var || !pVal) {
        return E_FAIL;
    }

    switch (var->vt) {

        case VT_INTEGER:
            *pVal = var->lVal;
            return S_OK;

        case VT_DECIMAL:
            *pVal = (LONG)var->dblVal;
            return S_OK;

        case VT_INTEGER64:
            temp = var->llVal;

            if (temp < (int64_t)LONG_MIN || temp > (int64_t)LONG_MAX) {
                return E_FAIL;
            }

            *pVal = (LONG)temp;
            return S_OK;

        case VT_UINTEGER64:
            if (var->ullVal > (uint64_t)LONG_MAX) {
                return E_FAIL;
            }

            *pVal = (LONG)var->ullVal;
            return S_OK;

    default:
        return E_FAIL;
    }
}

/*
* Caskey, Damon V.
* Orginal author (Utunels?) and date unknown.
* 
* Reworked 2026-06-02 to safely handle 64-bit 
* integers.
*
* Get a decimal value from a variant. 
* Will attempt to convert if possible. 
*/
HRESULT ScriptVariant_DecimalValue(ScriptVariant *var, DOUBLE *pVal)
{
    if (!var || !pVal) {
        return E_FAIL;
    }

    switch (var->vt) {
    case VT_INTEGER:
        *pVal = (DOUBLE)var->lVal;
        return S_OK;

    case VT_INTEGER64:
        *pVal = (DOUBLE)var->llVal;
        return S_OK;

    case VT_UINTEGER64:
        *pVal = (DOUBLE)var->ullVal;
        return S_OK;

    case VT_DECIMAL:
        *pVal = var->dblVal;
        return S_OK;

    default:
        return E_FAIL;
    }
}

/*
* Caskey, Damon V.
* 2026-06-02
*
* Get a 64-bit integer value from a variant. 
* Will attempt to convert if possible. 
*/
HRESULT ScriptVariant_Integer64Value(ScriptVariant *var, int64_t *pVal) {
    if (!var || !pVal) {
        return E_FAIL;
    }

    switch (var->vt) {
        case VT_INTEGER:
            *pVal = (int64_t)var->lVal;
            return S_OK;

        case VT_INTEGER64:
            *pVal = var->llVal;
            return S_OK;

        case VT_UINTEGER64:
            if (var->ullVal > (uint64_t)INT64_MAX) {
                return E_FAIL;
            }

            *pVal = (int64_t)var->ullVal;
            return S_OK;

        case VT_DECIMAL:
            *pVal = (int64_t)var->dblVal;
            return S_OK;

        default:
        return E_FAIL;
    }
}

/*
* Caskey, Damon V.
* 2026-06-02
*
* Get an unsigned 64-bit integer value from a variant. 
* Will attempt to convert if possible. 
*/
HRESULT ScriptVariant_Unsigned64Value(ScriptVariant *var, uint64_t *pVal) {
    if (!var || !pVal) {
        return E_FAIL;
    }

    switch (var->vt) {
    case VT_INTEGER:
        if (var->lVal < 0) {
            return E_FAIL;
        }

        *pVal = (uint64_t)var->lVal;
        return S_OK;

    case VT_INTEGER64:
        if (var->llVal < 0) {
            return E_FAIL;
        }

        *pVal = (uint64_t)var->llVal;
        return S_OK;

    case VT_UINTEGER64:
        *pVal = var->ullVal;
        return S_OK;

    case VT_DECIMAL:
        if (var->dblVal < 0.0) {
            return E_FAIL;
        }

        *pVal = (uint64_t)var->dblVal;
        return S_OK;

    default:
        return E_FAIL;
    }
}

/*
* Caskey, Damon V.
* Orginal author (Utunels?) and date unknown.
* 
* Reworked 2026-06-02 to handle 64-bit 
* integers.
*/
BOOL ScriptVariant_IsTrue(ScriptVariant *svar) {
    
    switch(svar->vt) {

        case VT_STR:
            return StrCache_Get(svar->strVal)[0] != 0;

        case VT_INTEGER:
            return svar->lVal != 0;

        case VT_INTEGER64:
            return svar->llVal != 0;

        case VT_UINTEGER64:
            return svar->ullVal != 0;

        case VT_DECIMAL:
            return svar->dblVal != 0.0;

        case VT_PTR:
            return svar->ptrVal != 0;

        default:
            return 0;
    }
}

/*
* Caskey, Damon V.
* Orginal author (Utunels?) and date unknown.
* 
* Reworked 2026-06-02 to handle 64-bit 
* integers.
*/
void ScriptVariant_ToString(ScriptVariant *svar, LPSTR buffer) {
    
    switch( svar->vt ) {
        case VT_EMPTY:
            sprintf( buffer, "<VT_EMPTY> Unitialized" );
            break;

        case VT_INTEGER:
            sprintf( buffer, "%ld", (long)svar->lVal);
            break;

        case VT_INTEGER64:
            sprintf( buffer, "%" PRId64, (int64_t)svar->llVal);
            break;

        case VT_UINTEGER64:
            sprintf( buffer, "%" PRIu64, (uint64_t)svar->ullVal);
            break;

        case VT_DECIMAL:
            sprintf( buffer, "%lf", svar->dblVal );
            break;

        case VT_PTR:
            sprintf(buffer, "#%ld", (long)(svar->ptrVal));
            break;

        case VT_STR:
            sprintf(buffer, "%s", StrCache_Get(svar->strVal));
            break;

        default:
            sprintf(buffer, "<Unprintable VARIANT type.>" );
            break;
    }
}

/*
* Caskey, Damon V.
* Orginal author (Utunels?) and date unknown.
* 
* Reworked 2026-06-02 to handle 64-bit integers.
*
* Get the length of a variant when converted to a string.
*/
static int ScriptVariant_LengthAsString(ScriptVariant *svar) {
    
    switch (svar->vt) {
        case VT_EMPTY:
            return snprintf(NULL, 0, "<VT_EMPTY> Unitialized");

        case VT_INTEGER:
            return snprintf(NULL, 0, "%ld", (long)svar->lVal);

        case VT_INTEGER64:
            return snprintf(NULL, 0, "%" PRId64, (int64_t)svar->llVal);

        case VT_UINTEGER64:
            return snprintf(NULL, 0, "%" PRIu64, (uint64_t)svar->ullVal);

        case VT_DECIMAL:
            return snprintf(NULL, 0, "%lf", svar->dblVal);

        case VT_PTR:
            return snprintf(NULL, 0, "#%ld", (long)(svar->ptrVal));

        case VT_STR:
            return snprintf(NULL, 0, "%s", StrCache_Get(svar->strVal));

        default:
            return snprintf(NULL, 0, "<Unprintable VARIANT type.>");
    }
}

/*
* Caskey, Damon V.
* Orginal author (Utunels?) and date unknown.
* 
* Reworked 2026-06-02 to handle 64-bit integers.
*
* Copy a variant. Handles reference counting for
* string types.
*/
void ScriptVariant_Copy(ScriptVariant *svar, ScriptVariant *rightChild )
{
    if(svar->vt == VT_STR)
    {
        StrCache_Collect(svar->strVal);
    }

    switch( rightChild->vt ) {
        case VT_INTEGER:
            svar->lVal = rightChild->lVal;
            break;

        case VT_INTEGER64:
            svar->llVal = rightChild->llVal;
            break;

        case VT_UINTEGER64:
            svar->ullVal = rightChild->ullVal;
            break;

        case VT_DECIMAL:
            svar->dblVal = rightChild->dblVal;
            break;

        case VT_PTR:
            svar->ptrVal = rightChild->ptrVal;
            break;

        case VT_STR:
            svar->strVal = rightChild->strVal;
            StrCache_Grab(svar->strVal);
            break;

        default:
            svar->ptrVal = NULL;
            break;
    }

    svar->vt = rightChild->vt;
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* The following functions determine the 
* appropriate type for a math operation
* based on the types of the operands, and
* set the result of a math operation, safely
* handling overflow and type promotion to 
* 64-bit integers when necessary.
*/

static int ScriptVariant_IsDecimalMath(ScriptVariant *left, ScriptVariant *right)
{
    return left->vt == VT_DECIMAL || right->vt == VT_DECIMAL;
}

static int ScriptVariant_IsUnsignedMath(ScriptVariant *left, ScriptVariant *right)
{
    return left->vt == VT_UINTEGER64 || right->vt == VT_UINTEGER64;
}

static int ScriptVariant_Is64BitMath(ScriptVariant *left, ScriptVariant *right)
{
    return left->vt == VT_INTEGER64 ||
           right->vt == VT_INTEGER64 ||
           left->vt == VT_UINTEGER64 ||
           right->vt == VT_UINTEGER64;
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* Set the result of a SIGNED integer math 
* operation. Handles overflow and type 
* promotion to 64-bit integers when necessary.
*/
static void ScriptVariant_SetSignedIntegerResult(ScriptVariant *retvar, int64_t value, int force64) {
    
    if (!force64 && value >= (int64_t)LONG_MIN && value <= (int64_t)LONG_MAX) {
        ScriptVariant_ChangeType(retvar, VT_INTEGER);
        retvar->lVal = (LONG)value;
    
    } else {

        ScriptVariant_ChangeType(retvar, VT_INTEGER64);
        retvar->llVal = value;
    }
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* Set the result of an UNSIGNED integer math 
* operation. Handles overflow and type 
* promotion to 64-bit integers when necessary.
*/
static void ScriptVariant_SetUnsignedIntegerResult(ScriptVariant *retvar, uint64_t value, int force64) {
    
    if (!force64 && value <= (uint64_t)LONG_MAX) {

        ScriptVariant_ChangeType(retvar, VT_INTEGER);
        retvar->lVal = (LONG)value;
    
    } else {

        ScriptVariant_ChangeType(retvar, VT_UINTEGER64);
        retvar->ullVal = value;
    }
}

// light version, for compiled call, faster than above, but not safe in some situations
ScriptVariant *ScriptVariant_Assign(ScriptVariant *svar, ScriptVariant *rightChild )
{
    ScriptVariant_Copy(svar, rightChild);
    return rightChild;
}


ScriptVariant *ScriptVariant_MulAssign(ScriptVariant *svar, ScriptVariant *rightChild )
{
    ScriptVariant_Copy(svar, ScriptVariant_Mul(svar, rightChild));
    return svar;
}


ScriptVariant *ScriptVariant_DivAssign(ScriptVariant *svar, ScriptVariant *rightChild )
{
    ScriptVariant_Copy(svar, ScriptVariant_Div(svar, rightChild));
    return svar;
}


ScriptVariant *ScriptVariant_AddAssign(ScriptVariant *svar, ScriptVariant *rightChild )
{
    ScriptVariant_Copy(svar, ScriptVariant_Add(svar, rightChild));
    return svar;
}


ScriptVariant *ScriptVariant_SubAssign(ScriptVariant *svar, ScriptVariant *rightChild )
{
    ScriptVariant_Copy(svar, ScriptVariant_Sub(svar, rightChild));
    return svar;
}


ScriptVariant *ScriptVariant_ModAssign(ScriptVariant *svar, ScriptVariant *rightChild )
{
    ScriptVariant_Copy(svar, ScriptVariant_Mod(svar, rightChild));
    return svar;
}

//Logical Operations

/*
* Logical OR.
* 
* i || x
*/
ScriptVariant *ScriptVariant_Or( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};
    retvar.lVal = (ScriptVariant_IsTrue(svar) || ScriptVariant_IsTrue(rightChild));
    return &retvar;
}


/*
* Logical AND.
* 
* i && x
*/
ScriptVariant *ScriptVariant_And( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};
    retvar.lVal = (ScriptVariant_IsTrue(svar) && ScriptVariant_IsTrue(rightChild));
    return &retvar;
}

/*
* Bitwise OR.
* 
* i | x
*/
ScriptVariant *ScriptVariant_Bit_Or( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    LONG l1, l2;
    if(ScriptVariant_IntegerValue(svar, &l1) == S_OK &&
            ScriptVariant_IntegerValue(rightChild, &l2) == S_OK)
    {
        retvar.vt = VT_INTEGER;
        retvar.lVal = l1 | l2;
    }
    else
    {
        ScriptVariant_Clear(&retvar);
    }

    return &retvar;
}

/*
* Bitwise XOR.
* 
* i ^ x
*/
ScriptVariant *ScriptVariant_Xor( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    LONG l1, l2;
    if(ScriptVariant_IntegerValue(svar, &l1) == S_OK &&
            ScriptVariant_IntegerValue(rightChild, &l2) == S_OK)
    {
        retvar.vt = VT_INTEGER;
        retvar.lVal = l1 ^ l2;
    }
    else
    {
        ScriptVariant_Clear(&retvar);
    }

    return &retvar;
}

/*
* Bitwise AND.
* 
* i & x
*/
ScriptVariant *ScriptVariant_Bit_And( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    LONG l1, l2;
    if(ScriptVariant_IntegerValue(svar, &l1) == S_OK &&
            ScriptVariant_IntegerValue(rightChild, &l2) == S_OK)
    {
        retvar.vt = VT_INTEGER;
        retvar.lVal = l1 & l2;
    }
    else
    {
        ScriptVariant_Clear(&retvar);
    }

    return &retvar;
}

/*
* Equal.
* 
* i == x
*/
ScriptVariant *ScriptVariant_Eq( ScriptVariant *svar, ScriptVariant *rightChild )
{
    DOUBLE dbl1, dbl2;
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};

    if(ScriptVariant_DecimalValue(svar, &dbl1) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &dbl2) == S_OK)
    {
        retvar.lVal = (dbl1 == dbl2);
    }
    else if(svar->vt == VT_STR && rightChild->vt == VT_STR)
    {
        retvar.lVal = !(strcmp(StrCache_Get(svar->strVal), StrCache_Get(rightChild->strVal)));
    }
    else if(svar->vt == VT_PTR && rightChild->vt == VT_PTR)
    {
        retvar.lVal = (svar->ptrVal == rightChild->ptrVal);
    }
    else if(svar->vt == VT_EMPTY && rightChild->vt == VT_EMPTY)
    {
        retvar.lVal = 1;
    }
    else
    {
        retvar.lVal = !(memcmp(svar, rightChild, sizeof(ScriptVariant)));
    }

    return &retvar;
}

/*
* Not equal.
* 
* i != x
*/
ScriptVariant *ScriptVariant_Ne( ScriptVariant *svar, ScriptVariant *rightChild )
{
    DOUBLE dbl1, dbl2;
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};

    if(ScriptVariant_DecimalValue(svar, &dbl1) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &dbl2) == S_OK)
    {
        retvar.lVal = (dbl1 != dbl2);
    }
    else if(svar->vt == VT_STR && rightChild->vt == VT_STR)
    {
        retvar.lVal = strcmp(StrCache_Get(svar->strVal), StrCache_Get(rightChild->strVal));
    }
    else if(svar->vt == VT_PTR && rightChild->vt == VT_PTR)
    {
        retvar.lVal = (svar->ptrVal != rightChild->ptrVal);
    }
    else if(svar->vt == VT_EMPTY && rightChild->vt == VT_EMPTY)
    {
        retvar.lVal = 0;
    }
    else
    {
        retvar.lVal = (memcmp(svar, rightChild, sizeof(ScriptVariant)) != 0);
    }

    return &retvar;
}

/*
* Less than.
* 
* i < x
*/
ScriptVariant *ScriptVariant_Lt( ScriptVariant *svar, ScriptVariant *rightChild )
{
    DOUBLE dbl1, dbl2;
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};

    if(ScriptVariant_DecimalValue(svar, &dbl1) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &dbl2) == S_OK)
    {
        retvar.lVal = (dbl1 < dbl2);
    }
    else if(svar->vt == VT_STR && rightChild->vt == VT_STR)
    {
        retvar.lVal = (strcmp(StrCache_Get(svar->strVal), StrCache_Get(rightChild->strVal)) < 0);
    }
    else if(svar->vt == VT_PTR && rightChild->vt == VT_PTR)
    {
        retvar.lVal = (svar->ptrVal < rightChild->ptrVal);
    }
    else if(svar->vt == VT_EMPTY || rightChild->vt == VT_EMPTY)
    {
        retvar.lVal = 0;
    }
    else
    {
        retvar.lVal = (memcmp(svar, rightChild, sizeof(ScriptVariant)) < 0);
    }

    return &retvar;
}


/*
* Greater than.
* 
* i > x
*/
ScriptVariant *ScriptVariant_Gt( ScriptVariant *svar, ScriptVariant *rightChild )
{
    DOUBLE dbl1, dbl2;
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};

    if(ScriptVariant_DecimalValue(svar, &dbl1) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &dbl2) == S_OK)
    {
        retvar.lVal = (dbl1 > dbl2);
    }
    else if(svar->vt == VT_STR && rightChild->vt == VT_STR)
    {
        retvar.lVal = (strcmp(StrCache_Get(svar->strVal), StrCache_Get(rightChild->strVal)) > 0);
    }
    else if(svar->vt == VT_PTR && rightChild->vt == VT_PTR)
    {
        retvar.lVal = (svar->ptrVal > rightChild->ptrVal);
    }
    else if(svar->vt == VT_EMPTY || rightChild->vt == VT_EMPTY)
    {
        retvar.lVal = 0;
    }
    else
    {
        retvar.lVal = (memcmp(svar, rightChild, sizeof(ScriptVariant)) > 0);
    }

    return &retvar;
}


/*
* Greater than or equal to.
* 
* i >= x
*/
ScriptVariant *ScriptVariant_Ge( ScriptVariant *svar, ScriptVariant *rightChild )
{
    DOUBLE dbl1, dbl2;
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};

    if(ScriptVariant_DecimalValue(svar, &dbl1) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &dbl2) == S_OK)
    {
        retvar.lVal = (dbl1 >= dbl2);
    }
    else if(svar->vt == VT_STR && rightChild->vt == VT_STR)
    {
        retvar.lVal = (strcmp(StrCache_Get(svar->strVal), StrCache_Get(rightChild->strVal)) >= 0);
    }
    else if(svar->vt == VT_PTR && rightChild->vt == VT_PTR)
    {
        retvar.lVal = (svar->ptrVal >= rightChild->ptrVal);
    }
    else if(svar->vt == VT_EMPTY || rightChild->vt == VT_EMPTY)
    {
        retvar.lVal = 0;
    }
    else
    {
        retvar.lVal = (memcmp(svar, rightChild, sizeof(ScriptVariant)) >= 0);
    }

    return &retvar;
}


/*
* Less than or equal to.
* 
* i <= x
*/
ScriptVariant *ScriptVariant_Le( ScriptVariant *svar, ScriptVariant *rightChild )
{
    DOUBLE dbl1, dbl2;
    static ScriptVariant retvar = {{.lVal = 0}, VT_INTEGER};

    if(ScriptVariant_DecimalValue(svar, &dbl1) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &dbl2) == S_OK)
    {
        retvar.lVal = (dbl1 <= dbl2);
    }
    else if(svar->vt == VT_STR && rightChild->vt == VT_STR)
    {
        retvar.lVal = (strcmp(StrCache_Get(svar->strVal), StrCache_Get(rightChild->strVal)) <= 0);
    }
    else if(svar->vt == VT_PTR && rightChild->vt == VT_PTR)
    {
        retvar.lVal = (svar->ptrVal <= rightChild->ptrVal);
    }
    else if(svar->vt == VT_EMPTY || rightChild->vt == VT_EMPTY)
    {
        retvar.lVal = 0;
    }
    else
    {
        retvar.lVal = (memcmp(svar, rightChild, sizeof(ScriptVariant)) <= 0);
    }

    return &retvar;
}


/*
* Left bitwise shift.
*/
ScriptVariant *ScriptVariant_Shl( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    LONG l1, l2;
    if(ScriptVariant_IntegerValue(svar, &l1) == S_OK &&
            ScriptVariant_IntegerValue(rightChild, &l2) == S_OK)
    {
        retvar.vt = VT_INTEGER;
        retvar.lVal = ((ULONG)l1) << ((ULONG)l2);
    }
    else
    {
        ScriptVariant_Clear(&retvar);
    }

    return &retvar;
}


/*
* Right bitwise shift.
*/
ScriptVariant *ScriptVariant_Shr( ScriptVariant *svar, ScriptVariant *rightChild )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    LONG l1, l2;
    if(ScriptVariant_IntegerValue(svar, &l1) == S_OK &&
            ScriptVariant_IntegerValue(rightChild, &l2) == S_OK)
    {
        retvar.vt = VT_INTEGER;
        retvar.lVal = ((ULONG)l1) >> ((ULONG)l2);
    }
    else
    {
        ScriptVariant_Clear(&retvar);
    }

    return &retvar;
}

/*
* Caskey, Damon V.
* 2026-06-02
*
* Safely add two signed 64-bit integers, 
* checking for overflow.
*
* Returns 1 on success, 0 on overflow.
*/
static int ScriptVariant_AddSigned64(int64_t left, int64_t right, int64_t *result) {

    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        return 0;
    }

    *result = left + right;
    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* Safely add two unsigned 64-bit integers,
* checking for overflow.
*
* Returns 1 on success, 0 on overflow.
*/
static int ScriptVariant_AddUnsigned64(uint64_t left, uint64_t right, uint64_t *result) {

    if (left > UINT64_MAX - right) {
        return 0;
    }

    *result = left + right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* Adds two script variants.
*
* i + x
*
* String operands concatenate as before. Decimal operands
* use floating-point arithmetic. Integer operands stay in
* integer space so 64-bit carriers do not lose precision
* through DOUBLE conversion.
*/

ScriptVariant *ScriptVariant_Add(ScriptVariant *svar, ScriptVariant *rightChild) {
    
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};

    /*
    * String concatenation behavior.
    *
    * If either operand is a string, convert both operands
    * to strings and concatenate them into a new cache entry.
    */
    if (svar->vt == VT_STR || rightChild->vt == VT_STR) {

        CHAR *destination_string;
        int length_a = ScriptVariant_LengthAsString(svar);
        int length_b = ScriptVariant_LengthAsString(rightChild);

        ScriptVariant_ChangeType(&retvar, VT_STR);

        /*
        * String variants store a string-cache index, not
        * inline text. Reserve a cache entry large enough for
        * both operands and get its writable character buffer.
        */
        retvar.strVal = StrCache_Pop(length_a + length_b);
        destination_string = StrCache_Get(retvar.strVal);

        /*
        * Fill the cache-owned buffer: left operand first,
        * then right operand at the end of the left text.
        */
        ScriptVariant_ToString(svar, destination_string);
        ScriptVariant_ToString(rightChild, destination_string + length_a);

        /*
        * Finalize the cache-owned buffer as a C string.
        * The cache releases it later through reference 
        * counting.
        */
        destination_string[length_a + length_b] = '\0';

        return &retvar;
    }

    /*
    * If either operand is a decimal, use decimal arithmetic.
    */
    if (ScriptVariant_IsDecimalMath(svar, rightChild)) {
        DOUBLE left;
        DOUBLE right;

        if (ScriptVariant_DecimalValue(svar, &left) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &right) == S_OK) {

            ScriptVariant_ChangeType(&retvar, VT_DECIMAL);
            retvar.dblVal = left + right;
        
        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Unsigned 64-bit arithmetic is used when either
    * operand is an unsigned 64-bit carrier. Legacy
    * integer operands remain in the signed path unless
    * a true unsigned 64-bit value participates.
    */
    if (ScriptVariant_IsUnsignedMath(svar, rightChild)) {

        uint64_t left;
        uint64_t right;
        uint64_t result;

        if (ScriptVariant_Unsigned64Value(svar, &left) == S_OK &&
            ScriptVariant_Unsigned64Value(rightChild, &right) == S_OK &&
            ScriptVariant_AddUnsigned64(left, right, &result)) {

            ScriptVariant_SetUnsignedIntegerResult(&retvar, result, 1);
        
        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Signed integer arithmetic covers legacy integers
    * and signed 64-bit carriers.
    */
    {
        int64_t left;
        int64_t right;
        int64_t result;
        int force64 = ScriptVariant_Is64BitMath(svar, rightChild);

        if (ScriptVariant_Integer64Value(svar, &left) == S_OK &&
            ScriptVariant_Integer64Value(rightChild, &right) == S_OK &&
            ScriptVariant_AddSigned64(left, right, &result)) {

            ScriptVariant_SetSignedIntegerResult(&retvar, result, force64);
        
        } else {
            ScriptVariant_Clear(&retvar);
        }
    }    

    return &retvar;
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* Safely subtract two signed 64-bit integers,
* checking for overflow.
*
* Returns 1 on success, 0 on overflow.
*/
static int ScriptVariant_SubSigned64(int64_t left, int64_t right, int64_t *result) {

    if ((right < 0 && left > INT64_MAX + right) ||
        (right > 0 && left < INT64_MIN + right)) {
        return 0;
    }

    *result = left - right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-04
*
* Safely subtract two unsigned 64-bit integers,
* checking for underflow.
*
* Returns 1 on success, 0 on underflow.
*/
static int ScriptVariant_SubUnsigned64(uint64_t left, uint64_t right, uint64_t *result) {

    if (left < right) {
        return 0;
    }

    *result = left - right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Subtracts two script variants.
*
* i - x
*
* Decimal operands use floating-point arithmetic.
* Integer operands stay in integer space so 64-bit
* carriers do not lose precision through DOUBLE
* conversion.
*/
ScriptVariant *ScriptVariant_Sub(ScriptVariant *svar, ScriptVariant *rightChild) {

    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};

    /*
    * If either operand is a decimal, use decimal arithmetic.
    */
    if (ScriptVariant_IsDecimalMath(svar, rightChild)) {
        DOUBLE left;
        DOUBLE right;

        if (ScriptVariant_DecimalValue(svar, &left) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &right) == S_OK) {

            ScriptVariant_ChangeType(&retvar, VT_DECIMAL);
            retvar.dblVal = left - right;

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Unsigned 64-bit arithmetic is used when either
    * operand is an unsigned 64-bit carrier. Subtraction
    * fails if the result would underflow below zero.
    */
    if (ScriptVariant_IsUnsignedMath(svar, rightChild)) {

        uint64_t left;
        uint64_t right;
        uint64_t result;

        if (ScriptVariant_Unsigned64Value(svar, &left) == S_OK &&
            ScriptVariant_Unsigned64Value(rightChild, &right) == S_OK &&
            ScriptVariant_SubUnsigned64(left, right, &result)) {

            ScriptVariant_SetUnsignedIntegerResult(&retvar, result, 1);

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Signed integer arithmetic covers legacy integers
    * and signed 64-bit carriers. 
    */
    {
        int64_t left;
        int64_t right;
        int64_t result;
        int force64 = ScriptVariant_Is64BitMath(svar, rightChild);

        if (ScriptVariant_Integer64Value(svar, &left) == S_OK &&
            ScriptVariant_Integer64Value(rightChild, &right) == S_OK &&
            ScriptVariant_SubSigned64(left, right, &result)) {

            ScriptVariant_SetSignedIntegerResult(&retvar, result, force64);

        } else {
            ScriptVariant_Clear(&retvar);
        }
    }

    return &retvar;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Safely multiply two signed 64-bit integers,
* checking for overflow.
*
* Returns 1 on success, 0 on overflow.
*/
static int ScriptVariant_MulSigned64(int64_t left, int64_t right, int64_t *result) {
    
    if (left > 0) {
        
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left)) {
            return 0;
        }
    
    } else if (left < 0) {

        if ((right > 0 && left < INT64_MIN / right) ||
            (right < 0 && left < INT64_MAX / right)) {
            return 0;
        }
    }

    *result = left * right;
    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Safely multiply two unsigned 64-bit integers,
* checking for overflow.
*
* Returns 1 on success, 0 on overflow.
*/
static int ScriptVariant_MulUnsigned64(uint64_t left, uint64_t right, uint64_t *result) {

    if (right && left > UINT64_MAX / right) {
        return 0;
    }

    *result = left * right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Multiplies two script variants.
*
* Decimal operands use floating-point arithmetic.
* Integer operands stay in integer space so 64-bit
* carriers do not lose precision through DOUBLE
* conversion.
*/
ScriptVariant *ScriptVariant_Mul(ScriptVariant *svar, ScriptVariant *rightChild) {

    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};

    /*
    * If either operand is a decimal, use decimal arithmetic.
    */
    if (ScriptVariant_IsDecimalMath(svar, rightChild)) {
        DOUBLE left;
        DOUBLE right;

        if (ScriptVariant_DecimalValue(svar, &left) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &right) == S_OK) {

            ScriptVariant_ChangeType(&retvar, VT_DECIMAL);
            retvar.dblVal = left * right;

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Unsigned 64-bit arithmetic is used when either
    * operand is an unsigned 64-bit carrier. Multiplication
    * fails if the result would overflow uint64_t.
    */
    if (ScriptVariant_IsUnsignedMath(svar, rightChild)) {

        uint64_t left;
        uint64_t right;
        uint64_t result;

        if (ScriptVariant_Unsigned64Value(svar, &left) == S_OK &&
            ScriptVariant_Unsigned64Value(rightChild, &right) == S_OK &&
            ScriptVariant_MulUnsigned64(left, right, &result)) {

            ScriptVariant_SetUnsignedIntegerResult(&retvar, result, 1);

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Signed integer arithmetic covers legacy integers
    * and signed 64-bit carriers.
    */
    {
        int64_t left;
        int64_t right;
        int64_t result;
        int force64 = ScriptVariant_Is64BitMath(svar, rightChild);

        if (ScriptVariant_Integer64Value(svar, &left) == S_OK &&
            ScriptVariant_Integer64Value(rightChild, &right) == S_OK &&
            ScriptVariant_MulSigned64(left, right, &result)) {

            ScriptVariant_SetSignedIntegerResult(&retvar, result, force64);

        } else {
            ScriptVariant_Clear(&retvar);
        }
    }

    return &retvar;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Safely divide two signed 64-bit integers.
* Fail on divide by zero and the one signed
* division overflow case.
*
* Returns 1 on success, 0 on failure.
*/
static int ScriptVariant_DivSigned64(int64_t left, int64_t right, int64_t *result) {

    /* 
    * Divide by 0 attempt.
    */

    if (right == 0) {        
        return 0;
    }

    /*
    * Overflow case: INT64_MIN / -1. This 
    * would produce a result of 2^63, which
    * cannot be represented in an int64_t.
    */

    if (left == INT64_MIN && right == -1) {
        return 0;
    }

    *result = left / right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Safely divide two unsigned 64-bit integers.
* Checks for divide by zero.
*
* Returns 1 on success, 0 on failure.
*/
static int ScriptVariant_DivUnsigned64(uint64_t left, uint64_t right, uint64_t *result) {

    if (right == 0) {
        return 0;
    }

    *result = left / right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Divides two script variants.
*
* i / x
*
* Decimal operands use floating-point arithmetic.
* Integer operands stay in integer space so 64-bit
* carriers do not lose precision through DOUBLE
* conversion.
*/
ScriptVariant *ScriptVariant_Div(ScriptVariant *svar, ScriptVariant *rightChild) {

    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};

    /*
    * If either operand is a decimal, use decimal arithmetic.
    */
    if (ScriptVariant_IsDecimalMath(svar, rightChild)) {
        DOUBLE left;
        DOUBLE right;

        if (ScriptVariant_DecimalValue(svar, &left) == S_OK &&
            ScriptVariant_DecimalValue(rightChild, &right) == S_OK &&
            right != 0.0) {

            ScriptVariant_ChangeType(&retvar, VT_DECIMAL);
            retvar.dblVal = left / right;

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Unsigned 64-bit arithmetic is used when either
    * operand is an unsigned 64-bit carrier. Division
    * fails if the divisor is zero.
    */
    if (ScriptVariant_IsUnsignedMath(svar, rightChild)) {

        uint64_t left;
        uint64_t right;
        uint64_t result;

        if (ScriptVariant_Unsigned64Value(svar, &left) == S_OK &&
            ScriptVariant_Unsigned64Value(rightChild, &right) == S_OK &&
            ScriptVariant_DivUnsigned64(left, right, &result)) {

            ScriptVariant_SetUnsignedIntegerResult(&retvar, result, 1);

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Signed integer arithmetic covers legacy integers
    * and signed 64-bit carriers.
    */
    {
        int64_t left;
        int64_t right;
        int64_t result;
        int force64 = ScriptVariant_Is64BitMath(svar, rightChild);

        if (ScriptVariant_Integer64Value(svar, &left) == S_OK &&
            ScriptVariant_Integer64Value(rightChild, &right) == S_OK &&
            ScriptVariant_DivSigned64(left, right, &result)) {

            ScriptVariant_SetSignedIntegerResult(&retvar, result, force64);

        } else {
            ScriptVariant_Clear(&retvar);
        }
    }

    return &retvar;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Safely perform signed 64-bit modulo.
* Fails on modulo by zero and the one signed
* division overflow case.
*
* Returns 1 on success, 0 on failure.
*/
static int ScriptVariant_ModSigned64(int64_t left, int64_t right, int64_t *result) {

    /*
    * Modulo by 0 attempt.
    */

    if (right == 0) {
        return 0;
    }

    /*
    * Overflow case: INT64_MIN % -1. Even though
    * the mathematical remainder is 0, C evaluates
    * this through signed division rules.
    */

    if (left == INT64_MIN && right == -1) {
        return 0;
    }

    *result = left % right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Safely perform unsigned 64-bit modulo.
* Fails on modulo by zero.
*
* Returns 1 on success, 0 on failure.
*/
static int ScriptVariant_ModUnsigned64(uint64_t left, uint64_t right, uint64_t *result) {

    if (right == 0) {
        return 0;
    }

    *result = left % right;

    return 1;
}

/*
* Caskey, Damon V.
* 2026-06-05
*
* Performs modulo on two script variants.
*
* i % x
*
* Integer operands stay in integer space so 64-bit
* carriers do not lose precision through DOUBLE
* conversion. Decimal operands follow legacy integer
* conversion semantics.
*/
ScriptVariant *ScriptVariant_Mod(ScriptVariant *svar, ScriptVariant *rightChild) {

    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};

    /*
    * Unsigned 64-bit modulo is used when either
    * operand is an unsigned 64-bit carrier. Modulo
    * fails if the divisor is zero.
    */
    if (ScriptVariant_IsUnsignedMath(svar, rightChild)) {

        uint64_t left;
        uint64_t right;
        uint64_t result;

        if (ScriptVariant_Unsigned64Value(svar, &left) == S_OK &&
            ScriptVariant_Unsigned64Value(rightChild, &right) == S_OK &&
            ScriptVariant_ModUnsigned64(left, right, &result)) {

            ScriptVariant_SetUnsignedIntegerResult(&retvar, result, 1);

        } else {
            ScriptVariant_Clear(&retvar);
        }

        return &retvar;
    }

    /*
    * Signed integer modulo covers legacy integers
    * and signed 64-bit carriers.
    */
    {
        int64_t left;
        int64_t right;
        int64_t result;
        int force64 = ScriptVariant_Is64BitMath(svar, rightChild);

        if (ScriptVariant_Integer64Value(svar, &left) == S_OK &&
            ScriptVariant_Integer64Value(rightChild, &right) == S_OK &&
            ScriptVariant_ModSigned64(left, right, &result)) {

            ScriptVariant_SetSignedIntegerResult(&retvar, result, force64);

        } else {
            ScriptVariant_Clear(&retvar);
        }
    }

    return &retvar;
}

// Unary Operations

// ++i
void ScriptVariant_Inc_Op(ScriptVariant *svar )
{
    switch(svar->vt) {
        case VT_DECIMAL:
            ++(svar->dblVal);
            break;

        case VT_INTEGER:
            ++(svar->lVal);
            break;

        case VT_INTEGER64:
            ++(svar->llVal);
            break;

        case VT_UINTEGER64:
            ++(svar->ullVal);
            break;

        default:
            break;
    }
}

// i++
ScriptVariant *ScriptVariant_Inc_Op2(ScriptVariant *svar )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    ScriptVariant_Copy(&retvar, svar);

    switch(svar->vt) {

        case VT_DECIMAL:
            svar->dblVal++;
            break;
        case VT_INTEGER:
            svar->lVal++;
            break;
        case VT_INTEGER64:
            svar->llVal++;
            break;
        case VT_UINTEGER64:
            svar->ullVal++;
            break;
        default:
            ScriptVariant_Clear(&retvar);
            break;
    }

    return &retvar;
}

// --i
void ScriptVariant_Dec_Op(ScriptVariant *svar )
{
    switch(svar->vt) {

        case VT_DECIMAL:
            --(svar->dblVal);
            break;

        case VT_INTEGER:
            --(svar->lVal);
            break;

        case VT_INTEGER64:
            --(svar->llVal);
            break;

        case VT_UINTEGER64:
            --(svar->ullVal);
            break;

        default:
            break;
    }
}

// i--
ScriptVariant *ScriptVariant_Dec_Op2(ScriptVariant *svar )
{
    static ScriptVariant retvar = {{.ptrVal = NULL}, VT_EMPTY};
    ScriptVariant_Copy(&retvar, svar);

    switch(svar->vt) {

        case VT_DECIMAL:
            svar->dblVal--;
            break;
        case VT_INTEGER:
            svar->lVal--;
            break;
        case VT_INTEGER64:
            svar->llVal--;
            break;
        case VT_UINTEGER64:
            svar->ullVal--;
            break;
        default:
            ScriptVariant_Clear(&retvar);
            break;
    }

    return &retvar;
}

//+i
void ScriptVariant_Pos( ScriptVariant *svar) {
    /*
    static ScriptVariant retvar = {{.ptrVal=NULL}, VT_EMPTY};
    switch(svar->vt)
    {
    case VT_DECIMAL:retvar.vt=VT_DECIMAL;retvar.dblVal = +(svar->dblVal);
    case VT_INTEGER:retvar.vt=VT_INTEGER;retvar.lVal = +(svar->lVal);
    default:break;
    }
    ScriptVariant_Copy(svar, &retvar);
    return svar;*/
}

//-i
void ScriptVariant_Neg( ScriptVariant *svar) {
    
    switch(svar->vt) {
        case VT_DECIMAL:
            svar->dblVal = -(svar->dblVal);
            break;

        case VT_INTEGER:
            svar->lVal = -(svar->lVal);
            break;

        case VT_INTEGER64:
            svar->llVal = -(svar->llVal);
            break;

        case VT_UINTEGER64:
            
            /*
            * Unsigned values cannot be meaningfully negated.
            * Convert to signed only if it fits.
            */

            if (svar->ullVal <= (uint64_t)INT64_MAX) {
                uint64_t temp = svar->ullVal;

                ScriptVariant_ChangeType(svar, VT_INTEGER64);
                svar->llVal = -(int64_t)temp;
            }
            break;

        default:
            break;
    }
}

// !i
void ScriptVariant_Boolean_Not(ScriptVariant *svar )
{
    /*
    static ScriptVariant retvar = {{.lVal=0}, VT_INTEGER};
    retvar.lVal = !ScriptVariant_IsTrue(svar);
    ScriptVariant_Copy(svar, &retvar);
    return svar;*/
    BOOL b = !ScriptVariant_IsTrue(svar);
    ScriptVariant_ChangeType(svar, VT_INTEGER);
    svar->lVal = b;

}

// ~i
void ScriptVariant_Bitwise_Not(ScriptVariant *svar ) {

    switch (svar->vt) {
        case VT_INTEGER:
            svar->lVal = ~(svar->lVal);
            break;

        case VT_INTEGER64:
            svar->llVal = ~(svar->llVal);
            break;

        case VT_UINTEGER64:
            svar->ullVal = ~(svar->ullVal);
            break;

        default:
            break;
    }
}



