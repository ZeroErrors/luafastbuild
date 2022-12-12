print("Hello Lua World!")

-- Settings("Test Settings")
-- DLL("Test DLL")

Exec('all', {
  ExecExecutable = 'C:\\Windows\\System32\\where.exe',
  ExecArguments = 'where.exe',
  ExecOutput = 'out.txt',
  ExecUseStdOutAsOutput = true,
  ExecAlways = true,
  ExecAlwaysShowOutput = true
})

--Print( '_WORKING_DIR_ = $_WORKING_DIR_$' )
--Print( '_FASTBUILD_VERSION_STRING_ = $_FASTBUILD_VERSION_STRING_$' )
--Print( '_FASTBUILD_VERSION_ = $_FASTBUILD_VERSION_$' )
--Print( '_FASTBUILD_EXE_PATH_ = $_FASTBUILD_EXE_PATH_$' )
