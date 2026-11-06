-- Health check for AKS Neovim integration (:checkhealth aks)
-- Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.

local health = vim.health or require("health")
local start = health.start or health.report_start
local ok = health.ok or health.report_ok
local warn = health.warn or health.report_warn
local error = health.error or health.report_error

local M = {}

function M.check()
    start("AKS Language Environment")

    -- Check aks compiler CLI
    if vim.fn.executable("aks") == 1 then
        local version = vim.fn.system({ "aks", "--version" })
        version = vim.trim(version)
        ok("aks CLI found: " .. version)
    else
        error("aks CLI not found in PATH", { "Install AKS or add it to PATH" })
    end

    -- Check aksls language server
    if vim.fn.executable("aksls") == 1 then
        ok("aksls (Language Server) found in PATH")
    else
        warn("aksls not found in PATH. LSP features will be unavailable.", { "Build aksls from the aks-lang repository" })
    end

    -- Check aksfmt formatter
    if vim.fn.executable("aksfmt") == 1 then
        ok("aksfmt formatter found in PATH")
    else
        warn("aksfmt not found in PATH. Auto-formatting will be unavailable.")
    end

    -- Check akslint linter
    if vim.fn.executable("akslint") == 1 then
        ok("akslint linter found in PATH")
    else
        warn("akslint not found in PATH. Quick linting will be unavailable.")
    end
end

return M
