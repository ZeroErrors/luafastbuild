#pragma once

#include "Tools/FBuild/FBuildCore/Graph/NodeGraph.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"

#include "Core/FileIO/PathUtils.h"

struct lua_State;

class LuaNodeGraph : public NodeGraph
{
public:
  static NodeGraph * Initialize( lua_State *L, const AString & file, const char * nodeGraphDBFile, bool forceMigration );

  LuaNodeGraph(lua_State * L);

  bool ParseFromRoot( const AString & file );
  bool ParseBffFile( BFFParser & bffParser, const AString & filename );
  bool ParseLuaFile( const char * file );

  void LuaMigrate( const NodeGraph & oldNodeGraph );

  lua_State * L;
};

// Copied from BFFTokenizer::ExpandIncludePath
void ExpandIncludePath( const AString & currentFile, AString & includePath );