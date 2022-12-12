#include "LFBuild.h"

#include "LuaNodeGraph.h"

#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"
#include "Tools/FBuild/FBuildCore/BFF/Functions/Function.h"
#include "Tools/FBuild/FBuildCore/Cache/Cache.h"
#include "Tools/FBuild/FBuildCore/Cache/CachePlugin.h"
#include "Tools/FBuild/FBuildCore/Graph/SettingsNode.h"
#include "Tools/FBuild/FBuildCore/Helpers/BuildProfiler.h"

#include "Core/Env/ErrorFormat.h"
#include "Core/FileIO/FileIO.h"
#include "Core/Mem/SmallBlockAllocator.h"
#include "Core/Profile/Profile.h"
#include "Core/Tracing/Tracing.h"

#include "lua.h"
#include "lualib.h"

// Note: Needed for memset
#include <memory.h>

// From Function.cpp
extern Array<const Function *> g_Functions;

l_noret luaG_runerrorL(lua_State* L, const char* fmt, ...);

static int binding_function( lua_State * L )
{
    lua_Debug ar;
    if (lua_getinfo(L, 0, "n", &ar) == 0)
    {
        luaG_runerrorL(L, "Failed to get FASTBuild function name");
        // TODO: error thrown above return 0;
    }

    const AString name = AString(ar.name);

    const BFFFile file = BFFFile();
    const BFFToken functionNameToken = BFFToken( file, file.GetSourceFileContents().Get(), BFFTokenType::Function, name);
    const Function * func = Function::Find(name);

    // Note: Copied from BFFParser::ParseFunction
    if ( func->IsUnique() && func->GetSeen() )
    {
        Error::Error_1020_FunctionCanOnlyBeInvokedOnce( &functionNameToken, func );
        luaG_runerrorL(L, "Function can only be invoked once");
        // TODO: error thrown above return 0;
    }
    func->SetSeen();

    int numArgs = lua_gettop(L); // number of arguments
    int headerArgs = numArgs;

    if (func->NeedsBody())
    {
        if (numArgs == 0)
        {
            // TODO: Make better error, since we pass the 'body' as a table in the last arg
            Error::Error_1024_FunctionRequiresABody( &functionNameToken, func );
            luaG_runerrorL(L, "Function requires a body");
            // TODO: error thrown above return 0;
        }

        // Don't parse the last arg as part of the header
        headerArgs--;
    }

    if (func->NeedsHeader() && headerArgs == 0)
    {
        Error::Error_1023_FunctionRequiresAHeader( &functionNameToken, func );
        luaG_runerrorL(L, "Function requires a header");
        // TODO: error thrown above return 0;
    }

    Array<BFFToken> headerTokens;
    if (headerArgs > 0)
    {
        // Note: Currently BFF only actually supports 1 string arg for headers
        //      but we parse any we got and let it error.
        for (int i = 1; i <= headerArgs; i++)
        {
            luaL_checktype(L, i, LUA_TSTRING);

            size_t len;
            const char* str = lua_tolstring(L, i, &len);

            headerTokens.Append(BFFToken(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::String,
                AString(str, str + len)
            ));
        }
    }

    Array<BFFToken> bodyTokens;
    if (func->NeedsBody())
    {
        // Validate that the last arg is a table
        luaL_checktype(L, numArgs, LUA_TTABLE);

        lua_pushnil(L); // first key
        while (lua_next(L, numArgs))
        {
            // Key is alawys a string
            luaL_checktype(L, -2, LUA_TSTRING);

            size_t keyLen;
            const char* key = lua_tolstring(L, -2, &keyLen);

            AString variable( "." );
            variable.Append(AString( key, key + keyLen ));

            bodyTokens.Append(BFFToken(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::Variable,
                variable
            ));

            bodyTokens.Append(BFFToken(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::Operator,
                AString( "=" )
            ));

            int type = lua_type(L, -1);
            switch (type)
            {
                case LUA_TNIL:
                    bodyTokens.Append(BFFToken(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::Invalid,
                        AString()
                    ));
                    break;
                case LUA_TBOOLEAN:
                    bodyTokens.Append(BFFToken(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::Boolean,
                        lua_toboolean(L, -1) == 1
                    ));
                    break;
                case LUA_TLIGHTUSERDATA:
                    // TODO: Support light userdata
                    ASSERT(false);
                    break;
                case LUA_TVECTOR:
                    // TODO: Support vector
                    ASSERT(false);
                    break;
                case LUA_TTABLE:
                    // TODO: Support table
                    ASSERT(false);
                    break;
                case LUA_TNUMBER:
                {
                    AString str;
                    str.AppendFormat("%d", lua_tonumber(L, -1));

                    bodyTokens.Append(BFFToken(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::Number,
                        str
                    ));
                }
                    break;
                case LUA_TSTRING:
                {
                    size_t valueLen;
                    const char* value = lua_tolstring(L, -1, &valueLen);

                    bodyTokens.Append(BFFToken(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::String,
                        AString( value, value + valueLen )
                    ));
                }
                    break;
            }

            lua_pop(L, 1); // remove value pushed by lua_next
        }
    }

    LuaUserdata * userdata = static_cast<LuaUserdata *>(lua_callbacks(L)->userdata);

    bool result = func->ParseFunction( *userdata->luaNodeGraph,
                                *userdata->bffParser,
                                &functionNameToken,
                                BFFTokenRange( headerTokens.Begin(), headerTokens.End() ),
                                BFFTokenRange( bodyTokens.Begin(), bodyTokens.End() ) );
    if (!result)
    {
        luaG_runerrorL(L, "Error executing function: %s", ar.name);
        // TODO: error thrown above return 0;
    }

    return 0;
}


LFBuild::LFBuild( const FBuildOptions &options ) : FBuild( options ), L(luaL_newstate())
{
    luaL_openlibs(L);

    size_t size = g_Functions.GetSize();
    luaL_Reg * bindings = FNEW_ARRAY( luaL_Reg [ size + 1 ] );
    memset( bindings, 0, sizeof( const luaL_Reg ) * ( size + 1 ) );

    size_t x = 0;
    size_t i = 0;
    for (; i < size; i++)
    {
        const Function * f = g_Functions[i];

        // Exclude functions that already exist in Lua or we use Lua langauge features instead.
        //if (f->GetName() == "Error") continue;
        if (f->GetName() == "ForEach") continue;
        if (f->GetName() == "If") continue;
        //if (f->GetName() == "Print") continue;
        if (f->GetName() == "Using") continue;

        bindings[x++] = { f->GetName().Get(), binding_function };
    }

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, NULL, bindings);
    lua_pop(L, 1);

    FDELETE_ARRAY bindings;

    static const luaL_Reg globalBindings[] = {
        // TODO: {"require", lua_require},
        {NULL, NULL}
    };

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, NULL, globalBindings);
    lua_pop(L, 1);

    luaL_sandbox(L);
    luaL_sandboxthread(L);

    // TODO: Allow BFF to call into Lua code.
    //      Implement FunctionIncludeLua.
    //      FunctionPrint seems to be a good example.
    //g_Functions.Append( FNEW( FunctionIncludeLua ) );
}

LFBuild::~LFBuild()
{
    lua_close(L);
    L = nullptr;
}

bool LFBuild::LuaInitialize( const char *nodeGraphDBFile )
{
    PROFILE_FUNCTION;
    BuildProfilerScope buildProfileScope( "Initialize" );

    // handle working dir
    if ( !FileIO::SetCurrentDir( m_Options.GetWorkingDir() ) )
    {
        FLOG_ERROR( "Failed to set working dir. Error: %s Dir: '%s'", LAST_ERROR_STR, m_Options.GetWorkingDir().Get() );
        return false;
    }

    // TODO: Check for default .lua file and use either GetDefaultBFFFileName() or lfbuild.lua

    // Note: @Modified: Renamed bffConfigFile to rootConfigFile and converted to AString
    const AString rootConfigFile = AString(m_Options.m_ConfigFile.IsEmpty() ? GetDefaultBFFFileName()
                                                            : m_Options.m_ConfigFile.Get());

    if ( nodeGraphDBFile != nullptr )
    {
        m_DependencyGraphFile = nodeGraphDBFile;
    }
    else
    {
        m_DependencyGraphFile = rootConfigFile;
        if ( m_DependencyGraphFile.EndsWithI( ".lua" ) || m_DependencyGraphFile.EndsWithI( ".bff" ) )
        {
            m_DependencyGraphFile.SetLength( m_DependencyGraphFile.GetLength() - 4 );
        }
        #if defined( __WINDOWS__ )
            m_DependencyGraphFile += ".windows.fdb";
        #elif defined( __OSX__ )
            m_DependencyGraphFile += ".osx.fdb";
        #elif defined( __LINUX__ )
            m_DependencyGraphFile += ".linux.fdb";
        #endif
    }

    SmallBlockAllocator::SetSingleThreadedMode( true );

    // Note: @Modified: Added .lua root config check
    if ( rootConfigFile.EndsWithI( ".lua" ) )
    {
        m_DependencyGraph = LuaNodeGraph::Initialize( L, rootConfigFile.Get(), m_DependencyGraphFile.Get(), m_Options.m_ForceDBMigration_Debug );
    }
    else
    {
        m_DependencyGraph = NodeGraph::Initialize( rootConfigFile.Get(), m_DependencyGraphFile.Get(), m_Options.m_ForceDBMigration_Debug );
    }

    SmallBlockAllocator::SetSingleThreadedMode( false );

    if ( m_DependencyGraph == nullptr )
    {
        return false;
    }

    const SettingsNode * settings = m_DependencyGraph->GetSettings();

    // if the cache is enabled, make sure the path is set and accessible
    if ( m_Options.m_UseCacheRead || m_Options.m_UseCacheWrite || m_Options.m_CacheInfo || m_Options.m_CacheTrim )
    {
        if ( !settings->GetCachePluginDLL().IsEmpty() )
        {
            m_Cache = FNEW( CachePlugin( settings->GetCachePluginDLL() ) );
        }
        else
        {
            m_Cache = FNEW( Cache() );
        }

        if ( m_Cache->Init( settings->GetCachePath(),
                            settings->GetCachePathMountPoint(),
                            m_Options.m_UseCacheRead,
                            m_Options.m_UseCacheWrite,
                            m_Options.m_CacheVerbose,
                            settings->GetCachePluginDLLConfig() ) == false )
        {
            m_Options.m_UseCacheRead = false;
            m_Options.m_UseCacheWrite = false;
            FDELETE m_Cache;
            m_Cache = nullptr;
        }
    }

    return true;
}
