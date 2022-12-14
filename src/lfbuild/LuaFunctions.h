class LuaNodeGraph;
class BFFParser;
struct lua_State;

struct LuaUserdata
{
  LuaNodeGraph * luaNodeGraph;
  BFFParser * bffParser;
};

void RegisterLuaFunctions(lua_State * L);
