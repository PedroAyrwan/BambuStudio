// Estoque e Precos - cadastros, estoque e historico persistidos em JSON (sem dependencias de GUI).
#ifndef slic3r_EstoquePrecosStore_hpp_
#define slic3r_EstoquePrecosStore_hpp_

#include "EstoquePrecosCore.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r { namespace EstoquePrecos {

// Rolo de filamento em estoque (aba Filamentos + saldo).
struct Rolo
{
    std::string id;
    std::string nome;
    std::string material;
    std::string marca;
    std::string cor;
    double preco_pago = 0.0; // R$ pago no rolo
    double peso_g     = 1000.0;
    double saldo_g    = 0.0; // gramas restantes em estoque
    double minimo_g   = 0.0; // alerta de estoque baixo

    double custo_por_grama() const { return (preco_pago > 0.0 && peso_g > 0.0) ? preco_pago / peso_g : 0.0; }
};

// Insumo (aba Insumos + saldo).
struct Insumo
{
    std::string id;
    std::string nome;
    std::string categoria; // Acabamento, Componente, Embalagem...
    double preco_pago = 0.0;
    double quantidade = 1.0; // quanto vem na embalagem
    std::string unidade = "un";
    double saldo      = 0.0;
    double minimo     = 0.0;

    double custo_unidade() const { return (preco_pago > 0.0 && quantidade > 0.0) ? preco_pago / quantidade : 0.0; }
};

// Movimento de estoque / registro de producao.
struct Movimento
{
    std::string data;      // AAAA-MM-DD HH:MM
    std::string tipo;      // "producao", "ajuste", "compra"
    std::string descricao; // produto ou motivo
    std::string item;      // nome do rolo/insumo
    double quantidade = 0.0; // negativo = saida
    std::string unidade;
    double valor = 0.0;      // R$ (preco unitario na producao)
};

struct ConsumoRolo   { std::string rolo_id;   double gramas_por_placa = 0.0; };
struct ConsumoInsumo { std::string insumo_id; double qtd_por_unidade  = 0.0; };

// Insumo escolhido no produto (para precificar).
struct ItemInsumo { std::string insumo_id; double qtd_por_unidade = 0.0; };

struct Dados
{
    ConfigNegocio            config;
    std::vector<Impressora>  impressoras;
    std::vector<Canal>       canais;
    std::vector<Rolo>        rolos;
    std::vector<Insumo>      insumos;
    std::vector<Movimento>   historico;
    // "perfil de filamento|#COR" do Bambu Studio -> id do rolo escolhido pela ultima vez.
    std::map<std::string, std::string> mapa_filamentos;
    // Ultimos valores usados na tela de precificacao.
    std::string ultimo_canal;
    std::string ultima_impressora;

    // Valores iniciais iguais aos exemplos da planilha (impressora, canais e configuracao).
    static Dados padrao();

    Rolo       *rolo(const std::string &id);
    Insumo     *insumo(const std::string &id);
    const Rolo *rolo(const std::string &id) const;
    const Insumo *insumo(const std::string &id) const;
    std::string novo_id(const std::string &prefixo) const;

    // Impressora cujo campo perfil_bambu (ou nome) aparece no nome do perfil do Bambu Studio.
    int impressora_para_perfil(const std::string &perfil_bambu) const;
    int indice_canal(const std::string &nome) const;

    // Desconta do estoque e grava o historico. Retorna avisos (ex.: saldo negativo).
    std::vector<std::string> confirmar_producao(const std::string &produto,
                                                int placas,
                                                int unidades_por_placa,
                                                const std::vector<ConsumoRolo> &rolos_usados,
                                                const std::vector<ConsumoInsumo> &insumos_usados,
                                                double preco_unitario,
                                                const std::string &data_hora);

    std::string to_json() const;
    // Retorna false e preenche erro se o texto nao for um JSON valido.
    bool from_json(const std::string &texto, std::string &erro);

    bool carregar(const std::string &caminho, std::string &erro);
    bool salvar(const std::string &caminho, std::string &erro) const;
};

}} // namespace Slic3r::EstoquePrecos

#endif
