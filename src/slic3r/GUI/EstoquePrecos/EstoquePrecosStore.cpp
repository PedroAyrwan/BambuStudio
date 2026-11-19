#include "EstoquePrecosStore.hpp"

#include "nlohmann/json.hpp"

#include <cstdio>
#include <sstream>

#ifdef ESTOQUE_SEM_BOOST
#include <fstream>
namespace estoque_io = std;
#else
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
namespace estoque_io = boost::nowide;
#endif

namespace Slic3r { namespace EstoquePrecos {

using json = nlohmann::json;

Dados Dados::padrao()
{
    Dados d;
    Impressora a1;
    a1.nome             = "Bambu Lab A1 Combo";
    a1.preco_compra     = 4304.15;
    a1.valor_residual   = 0.1;
    a1.vida_util_h      = 5000.0;
    a1.potencia_w       = 90.0;
    a1.manutencao_por_h = 0.4;
    a1.perfil_bambu     = "A1";
    d.impressoras.push_back(a1);

    struct C { const char *n; double com, fixa, pag; };
    const C canais[] = {
        {"Venda direta - Pix / dinheiro", 0.0, 0.0, 0.0},
        {"Venda direta - cartao (maquininha/link)", 0.0, 0.0, 0.04},
        {"Venda direta - cartao parcelado", 0.0, 0.0, 0.10},
        {"Shopee - CNPJ", 0.20, 4.0, 0.0},
        {"Shopee - CPF", 0.20, 7.0, 0.0},
        {"Mercado Livre - Classico", 0.14, 6.5, 0.0},
        {"Mercado Livre - Premium", 0.19, 6.5, 0.0},
        {"Elo7", 0.18, 0.0, 0.0},
        {"Loja propria (Nuvemshop/Shopify + gateway)", 0.0, 0.5, 0.05},
        {"Outro canal", 0.10, 0.0, 0.0},
    };
    for (const C &c : canais) {
        Canal k;
        k.nome           = c.n;
        k.comissao       = c.com;
        k.taxa_fixa      = c.fixa;
        k.taxa_pagamento = c.pag;
        d.canais.push_back(k);
    }
    d.ultimo_canal      = d.canais.front().nome;
    d.ultima_impressora = a1.nome;
    return d;
}

Rolo *Dados::rolo(const std::string &id)
{
    for (Rolo &r : rolos) if (r.id == id) return &r;
    return nullptr;
}
const Rolo *Dados::rolo(const std::string &id) const
{
    for (const Rolo &r : rolos) if (r.id == id) return &r;
    return nullptr;
}
Insumo *Dados::insumo(const std::string &id)
{
    for (Insumo &i : insumos) if (i.id == id) return &i;
    return nullptr;
}
const Insumo *Dados::insumo(const std::string &id) const
{
    for (const Insumo &i : insumos) if (i.id == id) return &i;
    return nullptr;
}

std::string Dados::novo_id(const std::string &prefixo) const
{
    for (int n = 1;; ++n) {
        std::string id = prefixo + std::to_string(n);
        bool usado = false;
        for (const Rolo &r : rolos) if (r.id == id) usado = true;
        for (const Insumo &i : insumos) if (i.id == id) usado = true;
        if (!usado) return id;
    }
}

int Dados::impressora_para_perfil(const std::string &perfil) const
{
    // Prioriza o perfil_bambu mais longo que aparece no nome (ex.: "A1 mini" antes de "A1").
    int melhor = -1;
    size_t melhor_len = 0;
    for (size_t i = 0; i < impressoras.size(); ++i) {
        const std::string &chave = impressoras[i].perfil_bambu.empty() ? impressoras[i].nome : impressoras[i].perfil_bambu;
        if (!chave.empty() && perfil.find(chave) != std::string::npos && chave.size() > melhor_len) {
            melhor = int(i);
            melhor_len = chave.size();
        }
    }
    return melhor;
}

int Dados::indice_canal(const std::string &nome) const
{
    for (size_t i = 0; i < canais.size(); ++i)
        if (canais[i].nome == nome) return int(i);
    return -1;
}

std::vector<std::string> Dados::confirmar_producao(const std::string &produto, int placas, int unidades_por_placa,
                                                   const std::vector<ConsumoRolo> &rolos_usados,
                                                   const std::vector<ConsumoInsumo> &insumos_usados,
                                                   double preco_unitario, const std::string &data_hora)
{
    std::vector<std::string> avisos;
    if (placas < 1) placas = 1;
    if (unidades_por_placa < 1) unidades_por_placa = 1;
    const int unidades = placas * unidades_por_placa;

    for (const ConsumoRolo &c : rolos_usados) {
        Rolo *r = rolo(c.rolo_id);
        if (r == nullptr || c.gramas_por_placa <= 0.0) continue;
        const double g = c.gramas_por_placa * placas;
        r->saldo_g -= g;
        Movimento m;
        m.data = data_hora; m.tipo = "producao"; m.descricao = produto; m.item = r->nome;
        m.quantidade = -g; m.unidade = "g"; m.valor = preco_unitario;
        historico.push_back(m);
        if (r->saldo_g < 0.0)
            avisos.push_back("Saldo negativo em \"" + r->nome + "\". Confira o estoque deste rolo.");
        else if (r->minimo_g > 0.0 && r->saldo_g < r->minimo_g)
            avisos.push_back("Estoque baixo: \"" + r->nome + "\".");
    }
    for (const ConsumoInsumo &c : insumos_usados) {
        Insumo *i = insumo(c.insumo_id);
        if (i == nullptr || c.qtd_por_unidade <= 0.0) continue;
        const double q = c.qtd_por_unidade * unidades;
        i->saldo -= q;
        Movimento m;
        m.data = data_hora; m.tipo = "producao"; m.descricao = produto; m.item = i->nome;
        m.quantidade = -q; m.unidade = i->unidade; m.valor = preco_unitario;
        historico.push_back(m);
        if (i->saldo < 0.0)
            avisos.push_back("Saldo negativo em \"" + i->nome + "\". Confira o estoque deste insumo.");
        else if (i->minimo > 0.0 && i->saldo < i->minimo)
            avisos.push_back("Estoque baixo: \"" + i->nome + "\".");
    }
    return avisos;
}

static Movimento movimento(const std::string &data, const char *tipo, const std::string &item, double qtd, const std::string &unidade, double valor)
{
    Movimento m;
    m.data = data; m.tipo = tipo; m.item = item; m.quantidade = qtd; m.unidade = unidade; m.valor = valor;
    m.descricao = std::string(tipo) == "compra" ? "Compra" : "Ajuste de estoque";
    return m;
}

bool Dados::registrar_compra_rolo(const std::string &id, double rolos, double preco_por_rolo, const std::string &data_hora)
{
    Rolo *r = rolo(id);
    if (r == nullptr || rolos <= 0.0) return false;
    if (preco_por_rolo > 0.0) r->preco_pago = preco_por_rolo;
    const double g = rolos * r->peso_g;
    r->saldo_g += g;
    historico.push_back(movimento(data_hora, "compra", r->nome, g, "g", r->preco_pago));
    return true;
}

bool Dados::registrar_compra_insumo(const std::string &id, double pacotes, double preco_por_pacote, const std::string &data_hora)
{
    Insumo *i = insumo(id);
    if (i == nullptr || pacotes <= 0.0) return false;
    if (preco_por_pacote > 0.0) i->preco_pago = preco_por_pacote;
    const double q = pacotes * i->quantidade;
    i->saldo += q;
    historico.push_back(movimento(data_hora, "compra", i->nome, q, i->unidade, i->preco_pago));
    return true;
}

bool Dados::ajustar_saldo_rolo(const std::string &id, double saldo_g, const std::string &data_hora)
{
    Rolo *r = rolo(id);
    if (r == nullptr) return false;
    const double dif = saldo_g - r->saldo_g;
    r->saldo_g = saldo_g;
    historico.push_back(movimento(data_hora, "ajuste", r->nome, dif, "g", 0.0));
    return true;
}

bool Dados::ajustar_saldo_insumo(const std::string &id, double saldo, const std::string &data_hora)
{
    Insumo *i = insumo(id);
    if (i == nullptr) return false;
    const double dif = saldo - i->saldo;
    i->saldo = saldo;
    historico.push_back(movimento(data_hora, "ajuste", i->nome, dif, i->unidade, 0.0));
    return true;
}

// ---------------------------------------------------------------- JSON

static json cfg_to_json(const ConfigNegocio &c)
{
    return json{{"tarifa_kwh", c.tarifa_kwh}, {"retirada_mensal", c.retirada_mensal}, {"horas_trabalho_mes", c.horas_trabalho_mes},
                {"custos_fixos_mensais", c.custos_fixos_mensais}, {"das_mei", c.das_mei}, {"regime_mei", c.regime_mei},
                {"horas_impressao_mes", c.horas_impressao_mes}, {"desperdicio", c.desperdicio}, {"reserva_falhas", c.reserva_falhas},
                {"aliquota_imposto", c.aliquota_imposto}, {"margem_padrao", c.margem_padrao}, {"publicidade", c.publicidade},
                {"devolucoes", c.devolucoes}, {"cupons", c.cupons}, {"arredondar_90", c.arredondar_90}};
}

static ConfigNegocio cfg_from_json(const json &j)
{
    ConfigNegocio c;
    c.tarifa_kwh           = j.value("tarifa_kwh", c.tarifa_kwh);
    c.retirada_mensal      = j.value("retirada_mensal", c.retirada_mensal);
    c.horas_trabalho_mes   = j.value("horas_trabalho_mes", c.horas_trabalho_mes);
    c.custos_fixos_mensais = j.value("custos_fixos_mensais", c.custos_fixos_mensais);
    c.das_mei              = j.value("das_mei", c.das_mei);
    c.regime_mei           = j.value("regime_mei", c.regime_mei);
    c.horas_impressao_mes  = j.value("horas_impressao_mes", c.horas_impressao_mes);
    c.desperdicio          = j.value("desperdicio", c.desperdicio);
    c.reserva_falhas       = j.value("reserva_falhas", c.reserva_falhas);
    c.aliquota_imposto     = j.value("aliquota_imposto", c.aliquota_imposto);
    c.margem_padrao        = j.value("margem_padrao", c.margem_padrao);
    c.publicidade          = j.value("publicidade", c.publicidade);
    c.devolucoes           = j.value("devolucoes", c.devolucoes);
    c.cupons               = j.value("cupons", c.cupons);
    c.arredondar_90        = j.value("arredondar_90", c.arredondar_90);
    return c;
}

std::string Dados::to_json() const
{
    json j;
    j["versao"] = 1;
    j["config"] = cfg_to_json(config);
    j["impressoras"] = json::array();
    for (const Impressora &i : impressoras)
        j["impressoras"].push_back({{"nome", i.nome}, {"preco_compra", i.preco_compra}, {"valor_residual", i.valor_residual},
                                    {"vida_util_h", i.vida_util_h}, {"potencia_w", i.potencia_w},
                                    {"manutencao_por_h", i.manutencao_por_h}, {"perfil_bambu", i.perfil_bambu}});
    j["canais"] = json::array();
    for (const Canal &c : canais)
        j["canais"].push_back({{"nome", c.nome}, {"comissao", c.comissao}, {"taxa_fixa", c.taxa_fixa}, {"taxa_pagamento", c.taxa_pagamento}});
    j["rolos"] = json::array();
    for (const Rolo &r : rolos)
        j["rolos"].push_back({{"id", r.id}, {"nome", r.nome}, {"material", r.material}, {"marca", r.marca}, {"cor", r.cor},
                              {"preco_pago", r.preco_pago}, {"peso_g", r.peso_g}, {"saldo_g", r.saldo_g}, {"minimo_g", r.minimo_g}});
    j["insumos"] = json::array();
    for (const Insumo &i : insumos)
        j["insumos"].push_back({{"id", i.id}, {"nome", i.nome}, {"categoria", i.categoria}, {"preco_pago", i.preco_pago},
                                {"quantidade", i.quantidade}, {"unidade", i.unidade}, {"saldo", i.saldo}, {"minimo", i.minimo}});
    j["historico"] = json::array();
    for (const Movimento &m : historico)
        j["historico"].push_back({{"data", m.data}, {"tipo", m.tipo}, {"descricao", m.descricao}, {"item", m.item},
                                  {"quantidade", m.quantidade}, {"unidade", m.unidade}, {"valor", m.valor}});
    j["mapa_filamentos"] = mapa_filamentos;
    j["ultimo_canal"] = ultimo_canal;
    j["ultima_impressora"] = ultima_impressora;
    return j.dump(2);
}

bool Dados::from_json(const std::string &texto, std::string &erro)
{
    json j;
    try {
        j = json::parse(texto);
    } catch (const std::exception &e) {
        erro = e.what();
        return false;
    }
    try {
        Dados d;
        if (j.contains("config")) d.config = cfg_from_json(j["config"]);
        if (j.contains("impressoras"))
            for (const json &x : j["impressoras"]) {
                Impressora i;
                i.nome             = x.value("nome", std::string());
                i.preco_compra     = x.value("preco_compra", 0.0);
                i.valor_residual   = x.value("valor_residual", 0.1);
                i.vida_util_h      = x.value("vida_util_h", 5000.0);
                i.potencia_w       = x.value("potencia_w", 90.0);
                i.manutencao_por_h = x.value("manutencao_por_h", 0.4);
                i.perfil_bambu     = x.value("perfil_bambu", std::string());
                d.impressoras.push_back(i);
            }
        if (j.contains("canais"))
            for (const json &x : j["canais"]) {
                Canal c;
                c.nome           = x.value("nome", std::string());
                c.comissao       = x.value("comissao", 0.0);
                c.taxa_fixa      = x.value("taxa_fixa", 0.0);
                c.taxa_pagamento = x.value("taxa_pagamento", 0.0);
                d.canais.push_back(c);
            }
        if (j.contains("rolos"))
            for (const json &x : j["rolos"]) {
                Rolo r;
                r.id         = x.value("id", std::string());
                r.nome       = x.value("nome", std::string());
                r.material   = x.value("material", std::string());
                r.marca      = x.value("marca", std::string());
                r.cor        = x.value("cor", std::string());
                r.preco_pago = x.value("preco_pago", 0.0);
                r.peso_g     = x.value("peso_g", 1000.0);
                r.saldo_g    = x.value("saldo_g", 0.0);
                r.minimo_g   = x.value("minimo_g", 0.0);
                if (r.id.empty()) r.id = d.novo_id("r");
                d.rolos.push_back(r);
            }
        if (j.contains("insumos"))
            for (const json &x : j["insumos"]) {
                Insumo i;
                i.id         = x.value("id", std::string());
                i.nome       = x.value("nome", std::string());
                i.categoria  = x.value("categoria", std::string());
                i.preco_pago = x.value("preco_pago", 0.0);
                i.quantidade = x.value("quantidade", 1.0);
                i.unidade    = x.value("unidade", std::string("un"));
                i.saldo      = x.value("saldo", 0.0);
                i.minimo     = x.value("minimo", 0.0);
                if (i.id.empty()) i.id = d.novo_id("i");
                d.insumos.push_back(i);
            }
        if (j.contains("historico"))
            for (const json &x : j["historico"]) {
                Movimento m;
                m.data       = x.value("data", std::string());
                m.tipo       = x.value("tipo", std::string());
                m.descricao  = x.value("descricao", std::string());
                m.item       = x.value("item", std::string());
                m.quantidade = x.value("quantidade", 0.0);
                m.unidade    = x.value("unidade", std::string());
                m.valor      = x.value("valor", 0.0);
                d.historico.push_back(m);
            }
        if (j.contains("mapa_filamentos") && j["mapa_filamentos"].is_object())
            d.mapa_filamentos = j["mapa_filamentos"].get<std::map<std::string, std::string>>();
        d.ultimo_canal      = j.value("ultimo_canal", std::string());
        d.ultima_impressora = j.value("ultima_impressora", std::string());
        if (d.canais.empty()) d.canais = padrao().canais;
        if (d.impressoras.empty()) d.impressoras = padrao().impressoras;
        *this = d;
    } catch (const std::exception &e) {
        erro = e.what();
        return false;
    }
    return true;
}

bool Dados::carregar(const std::string &caminho, std::string &erro)
{
    estoque_io::ifstream in(caminho.c_str(), std::ios::in | std::ios::binary);
    if (!in) {
        *this = padrao();
        return true; // primeira execucao: comeca com os valores da planilha
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str(), erro);
}

bool Dados::salvar(const std::string &caminho, std::string &erro) const
{
    const std::string tmp = caminho + ".tmp";
    {
        estoque_io::ofstream out(tmp.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) {
            erro = "Nao foi possivel gravar " + tmp;
            return false;
        }
        out << to_json();
        if (!out.good()) {
            erro = "Falha ao gravar " + tmp;
            return false;
        }
    }
    const std::string bak = caminho + ".bak";
    estoque_io::remove(bak.c_str());
    estoque_io::rename(caminho.c_str(), bak.c_str()); // pode falhar na primeira gravacao (arquivo ainda nao existe)
    if (estoque_io::rename(tmp.c_str(), caminho.c_str()) != 0) {
        erro = "Nao foi possivel substituir " + caminho;
        return false;
    }
    return true;
}

}} // namespace Slic3r::EstoquePrecos
