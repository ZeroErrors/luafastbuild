// Note: Modified version of FBuild.h
#pragma once

#include "Tools/FBuild/FBuildCore/FBuild.h"

struct lua_State;

class LFBuild : public FBuild
{
public:
    explicit LFBuild(const FBuildOptions &options = FBuildOptions());
    virtual ~LFBuild();

    bool LuaInitialize( const char *nodeGraphDBFile = nullptr );

protected:
    lua_State * L;
};
