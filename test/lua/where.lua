local mod = {}

function mod.where()
  Exec('all', {
    ExecExecutable = 'C:\\Windows\\System32\\where.exe',
    ExecArguments = 'where.exe',
    ExecOutput = 'out/out.txt',
    ExecUseStdOutAsOutput = true,
  })
end

return mod
