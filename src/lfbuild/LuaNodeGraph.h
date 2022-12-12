#include "Tools/FBuild/FBuildCore/Graph/NodeGraph.h"

struct lua_State;
class BFFParser;

class LuaNodeGraph : public NodeGraph
{
public:
  static NodeGraph * Initialize( lua_State *L, const char * file, const char * nodeGraphDBFile, bool forceMigration );

  bool ParseFromLuaRoot( lua_State *L, const char * file );
};

struct LuaUserdata
{
  LuaNodeGraph * luaNodeGraph;
  BFFParser * bffParser;
};
