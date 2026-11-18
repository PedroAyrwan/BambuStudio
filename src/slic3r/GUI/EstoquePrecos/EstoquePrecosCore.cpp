#include "EstoquePrecosCore.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace EstoquePrecos {

double Impressora::depreciacao_por_h() const
{
    return vida_util_h > 0.0 ? preco_compra * (1.0 - valor_residual) / vida_util_h : 0.0;
}

double Impressora::energia_por_h(double tarifa_kwh) const
{
    return potencia_w / 1000.0 * tarifa_kwh;
}

double Impressora::custo_maquina_por_h(double tarifa_kwh) const
{
    return manutencao_por_h + depreciacao_por_h() + energia_por_h(tarifa_kwh);
}

double ConfigNegocio::custo_hora_trabalho() const
{
    return horas_trabalho_mes > 0.0 ? retirada_mensal / horas_trabalho_mes : 0.0;
}

double ConfigNegocio::custos_fixos_considerados() const
{
    return custos_fixos_mensais + (regime_mei ? das_mei : 0.0);
}

double ConfigNegocio::custo_fixo_por_hora_impressao() const
{
    return horas_impressao_mes > 0.0 ? custos_fixos_considerados() / horas_impressao_mes : 0.0;
}

double arredondar_preco(double x, bool arredondar_90)
{
    if (x <= 0.0)
        return 0.0;
    // Pequena tolerancia para nao subir 1 centavo por erro de ponto flutuante.
    const double eps = 1e-9;
    if (arredondar_90) {
        const double c = std::ceil(x - eps);
        return (c - 0.1 >= x - eps) ? c - 0.1 : c + 0.9;
    }
    return std::ceil(x * 100.0 - eps) / 100.0;
}

Resultado calcular(const Trabalho &t, const Impressora &imp, const Canal &canal, const ConfigNegocio &cfg)
{
    Resultado r;
    const double u = std::max(1, t.unidades_por_placa);

    double material_placa = 0.0;
    for (const UsoFilamento &f : t.filamentos)
        material_placa += f.gramas * f.custo_por_grama;
    r.material = material_placa * (1.0 + cfg.desperdicio) / u;

    r.energia     = t.horas_impressao * imp.energia_por_h(cfg.tarifa_kwh) / u;
    r.depreciacao = t.horas_impressao * imp.depreciacao_por_h() / u;
    r.manutencao  = t.horas_impressao * imp.manutencao_por_h / u;
    r.maquina     = r.energia + r.depreciacao + r.manutencao;

    r.falhas      = (r.material + r.maquina) * cfg.reserva_falhas;
    r.fixos       = t.horas_impressao * cfg.custo_fixo_por_hora_impressao() / u;
    r.mao_de_obra = (t.min_preparo_por_placa / u + t.min_mao_obra_por_unid) / 60.0 * cfg.custo_hora_trabalho();
    r.insumos     = t.insumos_por_unid;
    r.embalagem   = t.embalagem_por_unid;
    r.arquivo     = t.arquivo_unidades > 0.0 ? t.arquivo_custo / t.arquivo_unidades : 0.0;
    r.outros      = t.outros_por_unid;

    r.custo_producao = r.material + r.maquina + r.falhas + r.fixos + r.mao_de_obra + r.insumos + r.embalagem + r.arquivo + r.outros;

    r.frete           = t.frete_vendedor;
    r.taxa_fixa       = canal.taxa_fixa;
    r.pct_sobre_venda = cfg.aliquota_imposto + canal.comissao + canal.taxa_pagamento + cfg.publicidade + cfg.devolucoes + cfg.cupons;
    r.margem          = t.margem < 0.0 ? cfg.margem_padrao : t.margem;

    const double base = r.custo_producao + r.frete + r.taxa_fixa;
    const double div  = 1.0 - r.pct_sobre_venda - r.margem;
    r.valido          = div > 0.0;
    r.preco_exato     = r.valido ? base / div : 0.0;
    r.preco           = arredondar_preco(r.preco_exato, cfg.arredondar_90);
    r.preco_minimo    = (1.0 - r.pct_sobre_venda) > 0.0 ? base / (1.0 - r.pct_sobre_venda) : 0.0;

    r.impostos       = r.preco * cfg.aliquota_imposto;
    r.comissao       = r.preco * canal.comissao;
    r.taxa_pagamento = r.preco * canal.taxa_pagamento;
    r.publicidade    = r.preco * cfg.publicidade;
    r.devolucoes     = r.preco * cfg.devolucoes;
    r.cupons         = r.preco * cfg.cupons;
    r.lucro          = r.preco * (1.0 - r.pct_sobre_venda) - base;
    r.margem_real    = r.preco > 0.0 ? r.lucro / r.preco : 0.0;
    return r;
}

}} // namespace Slic3r::EstoquePrecos
