#pragma once

#include "AccessBypass.h"

#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"
#include "Tools/FBuild/FBuildCore/BFF/Tokenizer/BFFTokenizer.h"
#include "Tools/FBuild/FBuildCore/Graph/NodeGraph.h"
#include "Tools/FBuild/FBuildCore/Graph/SettingsNode.h"

ACCESS_BYPASS_DEFINE_MEMBER(BFFParser, BFFTokenizer, m_Tokenizer);
ACCESS_BYPASS_DEFINE_MEMBER(BFFParser, const BFFFile *, m_CurrentBFFFile);
ACCESS_BYPASS_DEFINE_FUNC(BFFParser, void, (), CreateBuiltInVariables);
ACCESS_BYPASS_DEFINE_FUNC(BFFParser, const Array<BFFFile *> &, () const, GetUsedFiles);

ACCESS_BYPASS_DEFINE_MEMBER(NodeGraph, const SettingsNode *, m_Settings);
ACCESS_BYPASS_DEFINE_FUNC(NodeGraph, bool, (const char * filename), ParseFromRoot);
ACCESS_BYPASS_DEFINE_FUNC(NodeGraph, void, (const NodeGraph & oldNodeGraph), Migrate);

// Note: Copied from NodeGraph::UsedFile
struct UsedFile
{
    explicit UsedFile( const AString & fileName, uint64_t timeStamp, uint64_t dataHash ) : m_FileName( fileName ), m_TimeStamp( timeStamp ), m_DataHash( dataHash ) {}
    AString     m_FileName;
    uint64_t    m_TimeStamp;
    uint64_t    m_DataHash;
};
ACCESS_BYPASS_DEFINE_REINTERPRET_CAST_MEMBER(NodeGraph, Array< NodeGraph::UsedFile >, m_UsedFiles, Array< UsedFile >);
