/* Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved. */

#include "ddVersion.h"

#define DD_VERSION_STRING "v21.04.30"

namespace DevDriver
{

const char* GetVersionString()
{
    return DD_VERSION_STRING;
}

} // DevDriver
