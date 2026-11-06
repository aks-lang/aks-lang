-- Neovim integration for the AKS programming language
-- Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.

local M = {}

M.default_config = {
    cmd = { "aksls" },
    filetypes = { "aks" },
    root_markers = { "aks.json", ".git" },
    settings = {
        aks = {
            checkOnSave = true,
            formatOnSave = false,
            inlayHints = true,
            borrowChecker = true,
        },
    },
}

function M.setup(user_config)
    local config = vim.tbl_deep_extend("force", M.default_config, user_config or {})

    -- Register with nvim-lspconfig if installed
    local has_lspconfig, lspconfig = pcall(require, "lspconfig")
    if has_lspconfig then
        local configs = require("lspconfig.configs")
        if not configs.aksls then
            configs.aksls = {
                default_config = {
                    cmd = config.cmd,
                    filetypes = config.filetypes,
                    root_dir = function(fname)
                        local util = require("lspconfig.util")
                        return util.root_pattern(unpack(config.root_markers))(fname)
                            or util.path.dirname(fname)
                    end,
                    settings = config.settings,
                },
            }
        end
        lspconfig.aksls.setup(config)
    elseif vim.lsp and vim.lsp.start then
        -- Neovim 0.8+ native LSP autocommand
        vim.api.nvim_create_autocmd("FileType", {
            pattern = "aks",
            callback = function(args)
                local root_dir = vim.fs.root(args.buf, config.root_markers)
                vim.lsp.start({
                    name = "aksls",
                    cmd = config.cmd,
                    root_dir = root_dir,
                    settings = config.settings,
                })
            end,
        })
    end

    if config.settings.aks.formatOnSave then
        vim.api.nvim_create_autocmd("BufWritePre", {
            pattern = "*.aks",
            callback = function()
                M.format()
            end,
        })
    end
end

function M.run(file)
    file = file or vim.api.nvim_buf_get_name(0)
    vim.cmd("split | terminal aks run " .. vim.fn.shellescape(file))
end

function M.test(file)
    file = file or vim.api.nvim_buf_get_name(0)
    vim.cmd("split | terminal aks test " .. vim.fn.shellescape(file))
end

function M.format(file)
    file = file or vim.api.nvim_buf_get_name(0)
    local out = vim.fn.system({ "aksfmt", file })
    if vim.v.shell_error == 0 then
        vim.cmd("checktime")
    else
        vim.notify("aksfmt failed: " .. out, vim.log.levels.ERROR)
    end
end

function M.lint(file)
    file = file or vim.api.nvim_buf_get_name(0)
    local out = vim.fn.system({ "akslint", file })
    vim.notify(out, vim.log.levels.INFO, { title = "akslint" })
end

return M
