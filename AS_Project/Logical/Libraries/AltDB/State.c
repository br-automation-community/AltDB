/********************************************************************
 * Library: AltDB
 * File: State.c
 ********************************************************************
 * Storage definition for the connection pool (see State.h).
 ********************************************************************/
#include <bur/plctypes.h>
#include <runtime.h>
#include <AsTCP.h>
#include <AltDB.h>
#include "State.h"

ConnSlot gConn[ND_MAX_CONN];
