// Teste do nucleo de calculo contra os valores calculados pela planilha.
// Compilar: g++ -std=c++17 -I src tests/estoque_precos/test_core.cpp src/slic3r/GUI/EstoquePrecos/EstoquePrecosCore.cpp
#include "slic3r/GUI/EstoquePrecos/EstoquePrecosCore.hpp"
#include "casos_planilha.h"

#include <cmath>
#include <cstdio>

using namespace Slic3r::EstoquePrecos;

static int falhas = 0;

static void confere(const char *caso, const char *campo, double obtido, double esperado, double tol)
{
    if (std::fabs(obtido - esperado) > tol) {
        std::printf("FALHOU  %-45s %-12s obtido=%.10f esperado=%.10f\n", caso, campo, obtido, esperado);
        ++falhas;
    }
}

int main()
{
    ConfigNegocio cfg;
    cfg.tarifa_kwh           = CFG[0];
    cfg.retirada_mensal      = CFG[1];
    cfg.horas_trabalho_mes   = CFG[2];
    cfg.custos_fixos_mensais = CFG[3];
    cfg.das_mei              = CFG[4];
    cfg.regime_mei           = CFG[5] != 0.0;
    cfg.horas_impressao_mes  = CFG[6];
    cfg.desperdicio          = CFG[7];
    cfg.reserva_falhas       = CFG[8];
    cfg.aliquota_imposto     = CFG[9];
    cfg.margem_padrao        = CFG[10];
    cfg.publicidade          = CFG[11];
    cfg.devolucoes           = CFG[12];
    cfg.cupons               = CFG[13];
    cfg.arredondar_90        = CFG[14] != 0.0;

    int n = 0;
    for (const CasoPlanilha &c : CASOS) {
        Impressora imp;
        imp.preco_compra     = c.imp_preco;
        imp.valor_residual   = c.imp_resid;
        imp.vida_util_h      = c.imp_vida;
        imp.potencia_w       = c.imp_w;
        imp.manutencao_por_h = c.imp_man;
        Canal canal;
        canal.comissao       = c.comissao;
        canal.taxa_fixa      = c.taxa_fixa;
        canal.taxa_pagamento = c.pagamento;
        Trabalho t;
        for (int i = 0; i < 4; ++i)
            t.filamentos.push_back({c.g[i], c.rpg[i]});
        t.horas_impressao       = c.horas;
        t.unidades_por_placa    = c.unid;
        t.min_preparo_por_placa = c.prep;
        t.min_mao_obra_por_unid = c.mao;
        t.insumos_por_unid      = c.ins;
        t.embalagem_por_unid    = c.emb;
        t.outros_por_unid       = c.outros;
        t.arquivo_custo         = c.arqc;
        t.arquivo_unidades      = c.arqu;
        t.frete_vendedor        = c.frete;
        t.margem                = c.marg;

        Resultado r = calcular(t, imp, canal, cfg);
        confere(c.nome, "custo", r.custo_producao, c.custo, 1e-6);
        confere(c.nome, "preco", r.preco, c.preco, 1e-9);
        confere(c.nome, "lucro", r.lucro, c.lucro, 1e-6);
        confere(c.nome, "preco_min", r.preco_minimo, c.pmin, 1e-6);
        ++n;
    }

    // Arredondamento ,90 (exemplo do Leia-me: 41,37 -> 41,90).
    confere("arredondar_90", "41.37", arredondar_preco(41.37, true), 41.90, 1e-9);
    confere("arredondar_90", "41.95", arredondar_preco(41.95, true), 42.90, 1e-9);
    confere("arredondar_90", "41.90", arredondar_preco(41.90, true), 41.90, 1e-9);
    confere("roundup", "2.1078", arredondar_preco(2.107805282, false), 2.11, 1e-9);

    std::printf("%d casos da planilha, %d falhas\n", n, falhas);
    return falhas == 0 ? 0 : 1;
}
