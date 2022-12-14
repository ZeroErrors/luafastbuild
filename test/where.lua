local mod = {}

function mod.where()
  print("lua-where")
  Exec('lua-where', {
    ExecExecutable = 'C:\\Windows\\System32\\where.exe',
    ExecArguments = 'where.exe',
    ExecOutput = 'out/lua-where.txt',
    ExecUseStdOutAsOutput = true,
  })
end

return mod
