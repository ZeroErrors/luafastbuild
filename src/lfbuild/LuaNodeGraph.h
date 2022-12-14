#include "Tools/FBuild/FBuildCore/Graph/NodeGraph.h"

#include "AccessBypass.h"

ACCESS_BYPASS_DEFINE_MEMBER(NodeGraph, const SettingsNode *, m_Settings);

// Note: Copied from NodeGraph::UsedFile
struct UsedFile
{
    explicit UsedFile( const AString & fileName, uint64_t timeStamp, uint64_t dataHash ) : m_FileName( fileName ), m_TimeStamp( timeStamp ), m_DataHash( dataHash ) {}
    AString     m_FileName;
    uint64_t    m_TimeStamp;
    uint64_t    m_DataHash;
};
ACCESS_BYPASS_DEFINE_REINTERPRET_CAST_MEMBER(NodeGraph, Array< NodeGraph::UsedFile >, m_UsedFiles, Array< UsedFile >);

struct lua_State;
class BFFParser;

class LuaNodeGraph : public NodeGraph
{
public:
  static NodeGraph * Initialize( lua_State *L, const char * file, const char * nodeGraphDBFile, bool forceMigration );

  bool ParseFromLuaRoot( lua_State *L, const char * file );

  void LuaMigrate( const NodeGraph & oldNodeGraph );
};
