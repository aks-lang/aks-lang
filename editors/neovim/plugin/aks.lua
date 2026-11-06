-- Neovim command registrations for AKS
-- Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.

if vim.g.loaded_aks == 1 then
    return
end
vim.g.loaded_aks = 1

local aks = require("aks")

vim.api.nvim_create_user_command("AksRun", function(opts)
    local target = (opts.args ~= "") and opts.args or nil
    aks.run(target)
end, { nargs = "?", complete = "file", desc = "Run current AKS file or target" })

vim.api.nvim_create_user_command("AksTest", function(opts)
    local target = (opts.args ~= "") and opts.args or nil
    aks.test(target)
end, { nargs = "?", complete = "file", desc = "Run tests on current AKS file or directory" })

vim.api.nvim_create_user_command("AksFmt", function(opts)
    local target = (opts.args ~= "") and opts.args or nil
    aks.format(target)
end, { nargs = "?", complete = "file", desc = "Format current AKS file with aksfmt" })

vim.api.nvim_create_user_command("AksLint", function(opts)
    local target = (opts.args ~= "") and opts.args or nil
    aks.lint(target)
end, { nargs = "?", complete = "file", desc = "Lint current AKS file with akslint" })
