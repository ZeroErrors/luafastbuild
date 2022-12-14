#include "LFBuild.h"

#include "LuaNodeGraph.h"
#include "LuaFunctions.h"

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

#include "lualib.h"

LFBuild::LFBuild( const FBuildOptions &options ) : FBuild( options ), L(luaL_newstate())
{
    RegisterLuaFunctions(L);
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

    // Note: @Modified: Renamed bffConfigFile to rootConfigFile and converted to AString
    AString rootConfigFile;
    if (m_Options.m_ConfigFile.IsEmpty())
    {
        if (FileIO::FileExists("fbuild.lua"))
        {
            rootConfigFile = "fbuild.lua";
        }
        else
        {
            rootConfigFile = GetDefaultBFFFileName();
        }
    }
    else
    {
        rootConfigFile = m_Options.m_ConfigFile.Get();
    }


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
    m_DependencyGraph = LuaNodeGraph::Initialize( L, rootConfigFile, m_DependencyGraphFile.Get(), m_Options.m_ForceDBMigration_Debug );

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
