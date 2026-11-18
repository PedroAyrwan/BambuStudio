// Estoque e Precos - nucleo de calculo (sem dependencias de GUI).
// Reproduz as regras da planilha "Planilha_completa.xlsx" (abas Config, Calculadora e Produtos).
#ifndef slic3r_EstoquePrecosCore_hpp_
#define slic3r_EstoquePrecosCore_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace EstoquePrecos {

// Impressora cadastrada (aba Config, secao 1).
struct Impressora
{
    std::string nome;
    double preco_compra       = 0.0; // R$
    double valor_residual     = 0.1; // fracao (0.1 = 10%)
    double vida_util_h        = 5000.0;
    double potencia_w         = 90.0;
    double manutencao_por_h   = 0.4; // R$/h
    std::string perfil_bambu;        // nome (ou parte) do perfil de impressora do Bambu Studio

    double depreciacao_por_h() const;
    double energia_por_h(double tarifa_kwh) const;
    double custo_maquina_por_h(double tarifa_kwh) const;
};

// Canal de venda (aba Config, secao 8).
struct Canal
{
    std::string nome;
    double comissao       = 0.0; // fracao do preco
    double taxa_fixa      = 0.0; // R$ por item
    double taxa_pagamento = 0.0; // fracao do preco
};

// Configuracoes do negocio (aba Config, secoes 2 a 7).
struct ConfigNegocio
{
    double tarifa_kwh            = 1.0394;
    double retirada_mensal       = 2500.0;
    double horas_trabalho_mes    = 160.0;
    double custos_fixos_mensais  = 440.0;  // soma das despesas fixas (sem DAS)
    double das_mei               = 82.05;
    bool   regime_mei            = true;   // soma o DAS aos custos fixos
    double horas_impressao_mes   = 200.0;
    double desperdicio           = 0.05;
    double reserva_falhas        = 0.05;
    double aliquota_imposto      = 0.0;    // fracao sobre a venda
    double margem_padrao         = 0.5;
    double publicidade           = 0.03;
    double devolucoes            = 0.02;
    double cupons                = 0.03;
    bool   arredondar_90         = false;

    double custo_hora_trabalho() const;
    double custos_fixos_considerados() const;
    double custo_fixo_por_hora_impressao() const;
};

// Uso de um filamento na placa.
struct UsoFilamento
{
    double gramas          = 0.0;
    double custo_por_grama = 0.0;
};

// Dados de um trabalho (uma placa fatiada) para precificar.
struct Trabalho
{
    std::vector<UsoFilamento> filamentos;
    double horas_impressao        = 0.0;  // da placa inteira
    int    unidades_por_placa     = 1;
    double min_preparo_por_placa  = 0.0;  // modelagem/fatiamento + retirada
    double min_mao_obra_por_unid  = 0.0;  // pos-processamento, pintura, montagem, embalagem, atendimento
    double insumos_por_unid       = 0.0;  // pintura, componentes (R$)
    double embalagem_por_unid     = 0.0;  // R$
    double outros_por_unid        = 0.0;  // R$
    double arquivo_custo          = 0.0;  // R$ do modelo 3D
    double arquivo_unidades       = 0.0;  // unidades para diluir o arquivo
    double frete_vendedor         = 0.0;  // R$ por pedido
    double margem                 = -1.0; // < 0 = usa a margem padrao
};

struct Resultado
{
    // Custos por unidade
    double material           = 0.0;
    double maquina            = 0.0; // energia + depreciacao + manutencao
    double energia            = 0.0;
    double depreciacao        = 0.0;
    double manutencao         = 0.0;
    double falhas             = 0.0;
    double fixos              = 0.0;
    double mao_de_obra        = 0.0;
    double insumos            = 0.0;
    double embalagem          = 0.0;
    double arquivo            = 0.0;
    double outros             = 0.0;
    double custo_producao     = 0.0;
    // Venda
    double frete              = 0.0;
    double taxa_fixa          = 0.0;
    double pct_sobre_venda    = 0.0; // impostos + comissao + pagamento + publicidade + devolucoes + cupons
    double margem             = 0.0;
    double preco_exato        = 0.0;
    double preco              = 0.0; // arredondado
    double preco_minimo       = 0.0; // lucro zero
    double lucro              = 0.0;
    double margem_real        = 0.0;
    double impostos           = 0.0;
    double comissao           = 0.0;
    double taxa_pagamento     = 0.0;
    double publicidade        = 0.0;
    double devolucoes         = 0.0;
    double cupons             = 0.0;
    bool   valido             = false; // false quando os % somam 100% ou mais
};

// Regra de arredondamento da planilha: ROUNDUP(x;2) ou proximo valor terminado em ,90.
double arredondar_preco(double preco_exato, bool arredondar_90);

Resultado calcular(const Trabalho &t, const Impressora &imp, const Canal &canal, const ConfigNegocio &cfg);

}} // namespace Slic3r::EstoquePrecos

#endif
