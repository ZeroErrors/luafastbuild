print("Hello fbuild.lua")

mod = require "where"

execute_bff "test.bff"

mod.where()
