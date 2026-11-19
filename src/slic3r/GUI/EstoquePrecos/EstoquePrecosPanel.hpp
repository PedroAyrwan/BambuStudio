// Aba "Estoque e Precos": precificacao a partir do fatiamento + estoque de filamentos e insumos.
#ifndef slic3r_EstoquePrecosPanel_hpp_
#define slic3r_EstoquePrecosPanel_hpp_

#include "EstoquePrecosStore.hpp"

#include <wx/panel.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class wxNotebook;
class wxGrid;
class wxGridEvent;
class wxChoice;
class wxTextCtrl;
class wxSpinCtrl;
class wxStaticText;
class wxButton;
class wxFlexGridSizer;
class wxBoxSizer;
class wxScrolledWindow;

namespace Slic3r { namespace GUI {

// Tabela editavel generica (wxGrid + botoes) ligada a um vetor dos Dados.
class TabelaEstoque : public wxPanel
{
public:
    enum class Tipo { Texto, Numero, Percentual, Calculado };
    struct Coluna
    {
        wxString titulo;
        Tipo     tipo;
        int      largura;
        std::function<wxString(int linha)>                    ler;
        std::function<void(int linha, const wxString &valor)> gravar; // vazio para colunas calculadas
    };
    // Botao extra que age sobre a linha selecionada (ex.: "Registrar compra").
    using Acao = std::pair<wxString, std::function<void(int linha)>>;

    TabelaEstoque(wxWindow *parent, const wxString &ajuda, std::vector<Coluna> colunas,
                  std::function<int()> contar, std::function<void()> adicionar, std::function<void(int)> remover,
                  std::function<void()> ao_alterar, bool somente_leitura = false,
                  const wxString &rotulo_adicionar = wxString(), std::vector<Acao> acoes = {});

    void recarregar();
    void set_destaque(std::function<bool(int linha)> destaque) { m_destaque = std::move(destaque); }
    // Linha selecionada (ou do cursor); -1 se nenhuma.
    int linha_selecionada() const;

private:
    void on_cell_changed(wxGridEvent &evt);

    wxGrid                  *m_grid{nullptr};
    std::vector<Coluna>      m_colunas;
    std::function<int()>     m_contar;
    std::function<void()>    m_adicionar;
    std::function<void(int)> m_remover;
    std::function<void()>    m_ao_alterar;
    std::function<bool(int)> m_destaque;
    bool                     m_recarregando{false};
};

class EstoquePrecosPanel : public wxPanel
{
public:
    EstoquePrecosPanel(wxWindow *parent);
    ~EstoquePrecosPanel() override;

    // Chamado quando a aba e selecionada: le de novo a placa fatiada atual.
    void on_activate();

private:
    // Paginas
    wxWindow      *criar_pagina_precificar(wxNotebook *nb);
    wxWindow      *criar_pagina_config(wxNotebook *nb);
    TabelaEstoque *criar_tabela_rolos(wxNotebook *nb);
    TabelaEstoque *criar_tabela_insumos(wxNotebook *nb);
    TabelaEstoque *criar_tabela_impressoras(wxNotebook *nb);
    TabelaEstoque *criar_tabela_canais(wxNotebook *nb);
    TabelaEstoque *criar_tabela_historico(wxNotebook *nb);

    // Precificacao
    void ler_fatiamento();
    void reconstruir_filamentos();
    void reconstruir_insumos();
    void escolher_insumo();
    void atualizar_escolhas();
    void recalcular();
    void confirmar_producao();
    void relayout();

    // Persistencia
    void carregar();
    void salvar();
    void dados_alterados(); // salva, atualiza listas e recalcula
    void estoque_alterado(); // recarrega as tabelas de estoque e historico
    std::string caminho_arquivo() const;

    struct SlotFilamento
    {
        int           id = 0;  // indice do filamento no Bambu (0-based)
        std::string   perfil;  // nome do perfil de filamento
        std::string   cor;     // #RRGGBB
        std::string   tipo;    // PLA, PETG...
        double        gramas = 0.0;
        std::string   rolo_id;
        wxTextCtrl   *txt_gramas{nullptr};
        wxChoice     *ch_rolo{nullptr};
        wxStaticText *lbl_custo{nullptr};
    };

    struct LinhaInsumo
    {
        std::string   insumo_id;
        double        qtd = 1.0; // por peca
        wxStaticText *lbl_custo{nullptr};
    };

    EstoquePrecos::Dados       m_dados;
    std::vector<SlotFilamento> m_slots;
    std::vector<LinhaInsumo>   m_insumos_produto;

    wxNotebook    *m_notebook{nullptr};
    TabelaEstoque *m_tab_rolos{nullptr};
    TabelaEstoque *m_tab_insumos{nullptr};
    TabelaEstoque *m_tab_impressoras{nullptr};
    TabelaEstoque *m_tab_canais{nullptr};
    TabelaEstoque *m_tab_historico{nullptr};

    // Precificar: topo
    wxScrolledWindow *m_pag_preco{nullptr};
    wxStaticText     *m_lbl_preco{nullptr};
    wxStaticText     *m_lbl_minimo{nullptr};
    wxChoice         *m_ch_canal{nullptr};
    wxSpinCtrl       *m_spin_placas{nullptr};
    // Placa
    wxStaticText     *m_lbl_status{nullptr};
    wxFlexGridSizer  *m_sizer_filamentos{nullptr};
    // Campos principais
    wxTextCtrl       *m_txt_produto{nullptr};
    wxChoice         *m_ch_impressora{nullptr};
    wxSpinCtrl       *m_spin_unidades{nullptr};
    wxTextCtrl       *m_txt_min_mao{nullptr};
    // Insumos
    wxFlexGridSizer  *m_sizer_insumos{nullptr};
    // Resumo
    wxStaticText     *m_lbl_r_custo{nullptr};
    wxStaticText     *m_lbl_r_taxas{nullptr};
    wxStaticText     *m_lbl_r_reservas{nullptr};
    wxStaticText     *m_lbl_r_lucro{nullptr};
    wxStaticText     *m_lbl_r_preco{nullptr};
    wxStaticText     *m_lbl_avisos{nullptr};
    wxButton         *m_btn_detalhes{nullptr};
    wxTextCtrl       *m_txt_detalhes{nullptr};
    // Mais opcoes
    wxButton         *m_btn_mais{nullptr};
    wxPanel          *m_pnl_mais{nullptr};
    wxTextCtrl       *m_txt_horas{nullptr};
    wxTextCtrl       *m_txt_min_preparo{nullptr};
    wxTextCtrl       *m_txt_embalagem{nullptr};
    wxTextCtrl       *m_txt_outros{nullptr};
    wxTextCtrl       *m_txt_frete{nullptr};
    wxTextCtrl       *m_txt_arquivo{nullptr};
    wxTextCtrl       *m_txt_arquivo_unid{nullptr};
    wxTextCtrl       *m_txt_margem{nullptr};

    // Custos do negocio
    std::vector<std::function<void()>> m_cfg_recarregar;

    double m_ultimo_preco{0.0};
    bool   m_montando{false};
    bool   m_destruindo{false};
};

}} // namespace Slic3r::GUI

#endif
