#include "EstoquePrecosPanel.hpp"

#include "../GUI.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#include "../PartPlate.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace Slic3r { namespace GUI {

using namespace Slic3r::EstoquePrecos;

// ---------------------------------------------------------------- utilitarios

static wxString ep_u8(const char *s) { return wxString::FromUTF8(s); }
static wxString ep_u8(const std::string &s) { return wxString::FromUTF8(s.c_str()); }
static std::string ep_std8(const wxString &s) { return std::string(s.utf8_str()); }

// Aceita "1.234,56", "1234,56" ou "1234.56". Retorna padrao se vazio/invalido.
static double ep_num(const wxString &texto, double padrao = 0.0, bool *ok = nullptr)
{
    std::string s = ep_std8(texto);
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    if (s.rfind("R$", 0) == 0) s = s.substr(2);
    if (!s.empty() && s.back() == '%') s.pop_back();
    if (s.find(',') != std::string::npos) {
        s.erase(std::remove(s.begin(), s.end(), '.'), s.end()); // separador de milhar
        std::replace(s.begin(), s.end(), ',', '.');
    }
    if (ok) *ok = false;
    if (s.empty()) return padrao;
    std::istringstream in(s);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail()) return padrao;
    if (ok) *ok = true;
    return v;
}

static wxString ep_fmt(double v, int casas = 2)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", casas, v);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return ep_u8(s);
}

static wxString ep_reais(double v) { return ep_u8("R$ ") + ep_fmt(v, 2); }
static wxString ep_pct(double v) { return ep_fmt(v * 100.0, 1) + "%"; }

static std::string ep_agora()
{
    return ep_std8(wxDateTime::Now().Format("%Y-%m-%d %H:%M"));
}

static wxString ep_titulo() { return ep_u8("Estoque e Preços"); }

static wxStaticText *ep_titulo_secao(wxWindow *p, const wxString &texto)
{
    auto *lbl = new wxStaticText(p, wxID_ANY, texto);
    wxFont f = lbl->GetFont();
    f.SetPointSize(f.GetPointSize() + 2);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    lbl->SetFont(f);
    return lbl;
}

// ---------------------------------------------------------------- TabelaEstoque

TabelaEstoque::TabelaEstoque(wxWindow *parent, const wxString &ajuda, std::vector<Coluna> colunas,
                             std::function<int()> contar, std::function<void()> adicionar, std::function<void(int)> remover,
                             std::function<void()> ao_alterar, bool somente_leitura,
                             const wxString &rotulo_adicionar, std::vector<Acao> acoes)
    : wxPanel(parent, wxID_ANY)
    , m_colunas(std::move(colunas))
    , m_contar(std::move(contar))
    , m_adicionar(std::move(adicionar))
    , m_remover(std::move(remover))
    , m_ao_alterar(std::move(ao_alterar))
{
    SetBackgroundColour(*wxWHITE);
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    if (!ajuda.empty()) {
        auto *lbl = new wxStaticText(this, wxID_ANY, ajuda);
        lbl->Wrap(FromDIP(900));
        sizer->Add(lbl, 0, wxALL, FromDIP(8));
    }

    if (!somente_leitura) {
        auto *botoes = new wxBoxSizer(wxHORIZONTAL);
        auto *btn_add = new wxButton(this, wxID_ANY, rotulo_adicionar.empty() ? ep_u8("Adicionar") : rotulo_adicionar);
        botoes->Add(btn_add, 0, wxRIGHT, FromDIP(8));
        btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            if (!m_adicionar) return;
            m_adicionar();
            if (m_ao_alterar) m_ao_alterar();
            recarregar();
            int ultima = m_grid->GetNumberRows() - 1;
            if (ultima >= 0) {
                m_grid->SetGridCursor(ultima, 0);
                m_grid->MakeCellVisible(ultima, 0);
                m_grid->EnableCellEditControl();
            }
        });
        for (const Acao &acao : acoes) {
            auto *btn = new wxButton(this, wxID_ANY, acao.first);
            botoes->Add(btn, 0, wxRIGHT, FromDIP(8));
            std::function<void(int)> f = acao.second;
            btn->Bind(wxEVT_BUTTON, [this, f](wxCommandEvent &) {
                const int linha = linha_selecionada();
                if (linha < 0) {
                    wxMessageBox(ep_u8("Clique primeiro na linha do item."), ep_titulo(), wxOK | wxICON_INFORMATION, this);
                    return;
                }
                f(linha);
                if (m_ao_alterar) m_ao_alterar();
                recarregar();
            });
        }
        auto *btn_del = new wxButton(this, wxID_ANY, ep_u8("Remover"));
        botoes->Add(btn_del, 0);
        btn_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            if (!m_remover) return;
            const int linha = linha_selecionada();
            if (linha < 0) return;
            wxString nome = m_grid->GetCellValue(linha, 0);
            if (wxMessageBox(ep_u8("Remover \"") + nome + ep_u8("\"?"), ep_titulo(), wxYES_NO | wxICON_QUESTION, this) != wxYES)
                return;
            m_remover(linha);
            if (m_ao_alterar) m_ao_alterar();
            recarregar();
        });
        sizer->Add(botoes, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    }

    m_grid = new wxGrid(this, wxID_ANY);
    m_grid->CreateGrid(0, int(m_colunas.size()));
    m_grid->SetRowLabelSize(FromDIP(36));
    for (size_t c = 0; c < m_colunas.size(); ++c) {
        m_grid->SetColLabelValue(int(c), m_colunas[c].titulo);
        m_grid->SetColSize(int(c), FromDIP(m_colunas[c].largura));
    }
    m_grid->SetColLabelSize(FromDIP(40));
    m_grid->EnableEditing(!somente_leitura);
    m_grid->Bind(wxEVT_GRID_CELL_CHANGED, &TabelaEstoque::on_cell_changed, this);
    sizer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    SetSizer(sizer);
}

int TabelaEstoque::linha_selecionada() const
{
    wxArrayInt linhas = m_grid->GetSelectedRows();
    int linha = linhas.IsEmpty() ? m_grid->GetGridCursorRow() : linhas[0];
    if (linha < 0 || !m_contar || linha >= m_contar()) return -1;
    return linha;
}

void TabelaEstoque::recarregar()
{
    m_recarregando = true;
    const int n = m_contar ? m_contar() : 0;
    const int atual = m_grid->GetNumberRows();
    if (atual < n)
        m_grid->AppendRows(n - atual);
    else if (atual > n)
        m_grid->DeleteRows(n, atual - n);

    const wxColour cinza(242, 242, 242);
    const wxColour alerta(255, 220, 220);
    for (int r = 0; r < n; ++r) {
        const bool destacar = m_destaque && m_destaque(r);
        for (size_t c = 0; c < m_colunas.size(); ++c) {
            const Coluna &col = m_colunas[c];
            m_grid->SetCellValue(r, int(c), col.ler ? col.ler(r) : wxString());
            const bool calc = col.tipo == Tipo::Calculado || !col.gravar;
            m_grid->SetReadOnly(r, int(c), calc);
            m_grid->SetCellBackgroundColour(r, int(c), destacar ? alerta : (calc ? cinza : *wxWHITE));
            if (col.tipo != Tipo::Texto)
                m_grid->SetCellAlignment(r, int(c), wxALIGN_RIGHT, wxALIGN_CENTRE);
        }
    }
    m_grid->ForceRefresh();
    m_recarregando = false;
}

void TabelaEstoque::on_cell_changed(wxGridEvent &evt)
{
    if (m_recarregando) return;
    const int r = evt.GetRow();
    const int c = evt.GetCol();
    if (c < 0 || c >= int(m_colunas.size()) || r < 0 || r >= m_contar()) return;
    const Coluna &col = m_colunas[c];
    if (col.gravar) col.gravar(r, m_grid->GetCellValue(r, c));
    // Recarrega depois do evento para atualizar colunas calculadas sem mexer na grade durante a edicao.
    CallAfter([this]() {
        if (m_ao_alterar) m_ao_alterar();
        recarregar();
    });
}

// ---------------------------------------------------------------- EstoquePrecosPanel

EstoquePrecosPanel::EstoquePrecosPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(*wxWHITE);
    carregar();

    m_montando = true;
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    m_notebook = new wxNotebook(this, wxID_ANY);
    m_notebook->AddPage(criar_pagina_precificar(m_notebook), ep_u8("Precificar"), true);
    m_tab_rolos = criar_tabela_rolos(m_notebook);
    m_notebook->AddPage(m_tab_rolos, ep_u8("Estoque de filamentos"));
    m_tab_insumos = criar_tabela_insumos(m_notebook);
    m_notebook->AddPage(m_tab_insumos, ep_u8("Estoque de insumos"));
    m_tab_impressoras = criar_tabela_impressoras(m_notebook);
    m_notebook->AddPage(m_tab_impressoras, ep_u8("Impressoras"));
    m_tab_canais = criar_tabela_canais(m_notebook);
    m_notebook->AddPage(m_tab_canais, ep_u8("Canais de venda"));
    m_notebook->AddPage(criar_pagina_config(m_notebook), ep_u8("Custos do negócio"));
    m_tab_historico = criar_tabela_historico(m_notebook);
    m_notebook->AddPage(m_tab_historico, ep_u8("Histórico"));
    sizer->Add(m_notebook, 1, wxEXPAND | wxALL, FromDIP(4));
    SetSizer(sizer);

    for (TabelaEstoque *t : {m_tab_rolos, m_tab_insumos, m_tab_impressoras, m_tab_canais, m_tab_historico})
        t->recarregar();
    atualizar_escolhas();
    m_montando = false;
    recalcular();
}

EstoquePrecosPanel::~EstoquePrecosPanel()
{
    // Destroi os controles enquanto m_dados ainda existe (os callbacks das tabelas usam m_dados).
    m_destruindo = true;
    DestroyChildren();
}

std::string EstoquePrecosPanel::caminho_arquivo() const
{
    return data_dir() + "/estoque_precos.json";
}

void EstoquePrecosPanel::carregar()
{
    std::string erro;
    if (!m_dados.carregar(caminho_arquivo(), erro)) {
        BOOST_LOG_TRIVIAL(error) << "Estoque e Precos: falha ao ler " << caminho_arquivo() << ": " << erro;
        wxMessageBox(ep_u8("Não foi possível ler o arquivo de estoque:\n") + ep_u8(caminho_arquivo()) + "\n\n" + ep_u8(erro) +
                         ep_u8("\n\nO arquivo não será alterado até você mudar algum dado. Uma cópia de segurança fica em .bak."),
                     ep_titulo(), wxOK | wxICON_WARNING);
        m_dados = Dados::padrao();
    }
}

void EstoquePrecosPanel::salvar()
{
    std::string erro;
    if (!m_dados.salvar(caminho_arquivo(), erro)) {
        BOOST_LOG_TRIVIAL(error) << "Estoque e Precos: falha ao salvar: " << erro;
        if (m_lbl_status) m_lbl_status->SetLabel(ep_u8("ERRO ao salvar o estoque: ") + ep_u8(erro));
    }
}

void EstoquePrecosPanel::dados_alterados()
{
    if (m_destruindo) return;
    salvar();
    atualizar_escolhas();
    recalcular();
}

void EstoquePrecosPanel::estoque_alterado()
{
    for (TabelaEstoque *t : {m_tab_rolos, m_tab_insumos, m_tab_historico})
        if (t) t->recarregar();
}

void EstoquePrecosPanel::on_activate()
{
    if (m_montando || m_destruindo) return;
    ler_fatiamento();
}

void EstoquePrecosPanel::relayout()
{
    if (m_pag_preco == nullptr) return;
    m_pag_preco->Layout();
    m_pag_preco->FitInside();
}

// ---------------------------------------------------------------- pagina Precificar

static wxTextCtrl *ep_campo(wxWindow *parent, wxFlexGridSizer *grid, const wxString &rotulo, const wxString &valor, const wxString &dica = wxString())
{
    grid->Add(new wxStaticText(parent, wxID_ANY, rotulo), 0, wxALIGN_CENTER_VERTICAL);
    auto *txt = new wxTextCtrl(parent, wxID_ANY, valor, wxDefaultPosition, wxSize(parent->FromDIP(110), -1));
    if (!dica.empty()) txt->SetToolTip(dica);
    grid->Add(txt, 0);
    return txt;
}

wxWindow *EstoquePrecosPanel::criar_pagina_precificar(wxNotebook *nb)
{
    m_pag_preco = new wxScrolledWindow(nb, wxID_ANY);
    m_pag_preco->SetBackgroundColour(*wxWHITE);
    m_pag_preco->SetScrollRate(FromDIP(12), FromDIP(12));
    wxWindow *p = m_pag_preco;
    const int borda = FromDIP(10);

    auto *col = new wxBoxSizer(wxVERTICAL);

    // ---------- Topo: preco + canal + producao
    auto *topo = new wxBoxSizer(wxHORIZONTAL);
    auto *bloco_preco = new wxBoxSizer(wxVERTICAL);
    m_lbl_preco = new wxStaticText(p, wxID_ANY, ep_u8("R$ —"));
    wxFont fp = m_lbl_preco->GetFont();
    fp.SetPointSize(fp.GetPointSize() + 14);
    fp.SetWeight(wxFONTWEIGHT_BOLD);
    m_lbl_preco->SetFont(fp);
    m_lbl_preco->SetForegroundColour(wxColour(0, 150, 80));
    bloco_preco->Add(new wxStaticText(p, wxID_ANY, ep_u8("Preço sugerido por peça")), 0);
    bloco_preco->Add(m_lbl_preco, 0);
    m_lbl_minimo = new wxStaticText(p, wxID_ANY, wxEmptyString);
    m_lbl_minimo->SetForegroundColour(wxColour(110, 110, 110));
    bloco_preco->Add(m_lbl_minimo, 0);
    topo->Add(bloco_preco, 0, wxRIGHT, FromDIP(30));

    auto *bloco_venda = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
    bloco_venda->Add(new wxStaticText(p, wxID_ANY, ep_u8("Vender em")), 0, wxALIGN_CENTER_VERTICAL);
    m_ch_canal = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(280), -1));
    bloco_venda->Add(m_ch_canal, 0);
    bloco_venda->Add(new wxStaticText(p, wxID_ANY, ep_u8("Placas produzidas")), 0, wxALIGN_CENTER_VERTICAL);
    auto *linha_prod = new wxBoxSizer(wxHORIZONTAL);
    m_spin_placas = new wxSpinCtrl(p, wxID_ANY, "1", wxDefaultPosition, wxSize(FromDIP(70), -1), wxSP_ARROW_KEYS, 1, 1000, 1);
    linha_prod->Add(m_spin_placas, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    auto *btn_confirmar = new wxButton(p, wxID_ANY, ep_u8("Confirmar produção"));
    btn_confirmar->SetToolTip(ep_u8("Desconta do estoque o filamento e os insumos usados e registra no histórico."));
    linha_prod->Add(btn_confirmar, 0, wxALIGN_CENTER_VERTICAL);
    bloco_venda->Add(linha_prod, 0);
    topo->Add(bloco_venda, 0, wxALIGN_CENTER_VERTICAL);
    col->Add(topo, 0, wxALL, borda);
    btn_confirmar->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { confirmar_producao(); });

    // ---------- Resumo
    auto *resumo = new wxFlexGridSizer(2, FromDIP(3), FromDIP(30));
    auto linha_resumo = [&](const char *rotulo, wxStaticText *&valor, bool destaque) {
        auto *l = new wxStaticText(p, wxID_ANY, ep_u8(rotulo));
        valor = new wxStaticText(p, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
        if (destaque) {
            l->SetFont(l->GetFont().Bold());
            valor->SetFont(valor->GetFont().Bold());
        }
        resumo->Add(l, 0);
        resumo->Add(valor, 0, wxALIGN_RIGHT);
    };
    linha_resumo("Custo para produzir", m_lbl_r_custo, false);
    linha_resumo("Taxas do canal e impostos", m_lbl_r_taxas, false);
    linha_resumo("Reservas (anúncios, devoluções, cupons)", m_lbl_r_reservas, false);
    linha_resumo("Seu lucro", m_lbl_r_lucro, true);
    linha_resumo("= Preço de venda", m_lbl_r_preco, true);
    col->Add(resumo, 0, wxLEFT | wxRIGHT, borda);
    m_lbl_avisos = new wxStaticText(p, wxID_ANY, wxEmptyString);
    m_lbl_avisos->SetForegroundColour(wxColour(190, 90, 0));
    col->Add(m_lbl_avisos, 0, wxLEFT | wxRIGHT | wxTOP, borda);

    m_btn_detalhes = new wxButton(p, wxID_ANY, ep_u8("Ver detalhes do cálculo e outros canais ▼"));
    col->Add(m_btn_detalhes, 0, wxLEFT | wxTOP, borda);
    m_txt_detalhes = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(700), FromDIP(430)),
                                    wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_txt_detalhes->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    m_txt_detalhes->Hide();
    col->Add(m_txt_detalhes, 0, wxLEFT | wxTOP, borda);
    m_btn_detalhes->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        const bool mostrar = !m_txt_detalhes->IsShown();
        m_txt_detalhes->Show(mostrar);
        m_btn_detalhes->SetLabel(mostrar ? ep_u8("Esconder detalhes ▲") : ep_u8("Ver detalhes do cálculo e outros canais ▼"));
        relayout();
    });

    // ---------- Placa fatiada
    col->Add(ep_titulo_secao(p, ep_u8("Placa fatiada")), 0, wxLEFT | wxTOP, borda);
    auto *linha_fat = new wxBoxSizer(wxHORIZONTAL);
    auto *btn_ler = new wxButton(p, wxID_ANY, ep_u8("Ler de novo"));
    btn_ler->SetToolTip(ep_u8("Fatie a placa em Preparar. Os dados também são lidos sempre que você abre esta aba."));
    linha_fat->Add(btn_ler, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_lbl_status = new wxStaticText(p, wxID_ANY, ep_u8("Fatie a placa em Preparar e volte aqui."));
    linha_fat->Add(m_lbl_status, 0, wxALIGN_CENTER_VERTICAL);
    col->Add(linha_fat, 0, wxLEFT | wxTOP, borda);
    btn_ler->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ler_fatiamento(); });
    m_sizer_filamentos = new wxFlexGridSizer(4, FromDIP(4), FromDIP(10));
    col->Add(m_sizer_filamentos, 0, wxLEFT | wxTOP, borda);

    // ---------- Peca
    col->Add(ep_titulo_secao(p, ep_u8("Peça")), 0, wxLEFT | wxTOP, borda);
    auto *g = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Nome")), 0, wxALIGN_CENTER_VERTICAL);
    m_txt_produto = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(320), -1));
    g->Add(m_txt_produto, 0);
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Impressora")), 0, wxALIGN_CENTER_VERTICAL);
    m_ch_impressora = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(320), -1));
    g->Add(m_ch_impressora, 0);
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Peças por placa")), 0, wxALIGN_CENTER_VERTICAL);
    m_spin_unidades = new wxSpinCtrl(p, wxID_ANY, "1", wxDefaultPosition, wxSize(FromDIP(110), -1), wxSP_ARROW_KEYS, 1, 10000, 1);
    g->Add(m_spin_unidades, 0);
    m_txt_min_mao = ep_campo(p, g, ep_u8("Seu trabalho por peça (minutos)"), "0",
                             ep_u8("Acabamento, pintura, montagem, embalagem e postagem de cada peça."));
    col->Add(g, 0, wxLEFT | wxTOP, borda);

    // ---------- Insumos
    col->Add(ep_titulo_secao(p, ep_u8("Insumos por peça")), 0, wxLEFT | wxTOP, borda);
    auto *dica_ins = new wxStaticText(p, wxID_ANY, ep_u8("Tinta, argola, ímã, embalagem... escolha do seu estoque e diga quanto vai em cada peça."));
    dica_ins->SetForegroundColour(wxColour(110, 110, 110));
    col->Add(dica_ins, 0, wxLEFT | wxTOP, borda);
    m_sizer_insumos = new wxFlexGridSizer(4, FromDIP(4), FromDIP(10));
    col->Add(m_sizer_insumos, 0, wxLEFT | wxTOP, borda);
    auto *btn_ins = new wxButton(p, wxID_ANY, ep_u8("+ Adicionar insumo"));
    col->Add(btn_ins, 0, wxLEFT | wxTOP, borda);
    btn_ins->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { escolher_insumo(); });

    // ---------- Mais opcoes
    m_btn_mais = new wxButton(p, wxID_ANY, ep_u8("Mais opções ▼"));
    col->Add(m_btn_mais, 0, wxLEFT | wxTOP, borda);
    m_pnl_mais = new wxPanel(p, wxID_ANY);
    m_pnl_mais->SetBackgroundColour(*wxWHITE);
    auto *gm = new wxFlexGridSizer(2, FromDIP(6), FromDIP(10));
    wxWindow *pm = m_pnl_mais;
    m_txt_horas        = ep_campo(pm, gm, ep_u8("Tempo de impressão da placa (horas)"), "0", ep_u8("Vem do fatiamento. Só mude se quiser."));
    m_txt_min_preparo  = ep_campo(pm, gm, ep_u8("Preparo e retirada da placa (minutos)"), "0", ep_u8("Fatiar, tirar a peça e limpar a mesa (uma vez por placa)."));
    m_txt_embalagem    = ep_campo(pm, gm, ep_u8("Embalagem avulsa (R$ por peça)"), "0", ep_u8("Use só se a embalagem não estiver nos insumos."));
    m_txt_outros       = ep_campo(pm, gm, ep_u8("Outros custos (R$ por peça)"), "0");
    m_txt_frete        = ep_campo(pm, gm, ep_u8("Frete pago por você (R$ por pedido)"), "0");
    m_txt_arquivo      = ep_campo(pm, gm, ep_u8("Custo do arquivo 3D (R$)"), "0");
    m_txt_arquivo_unid = ep_campo(pm, gm, ep_u8("Peças para pagar o arquivo"), "100");
    m_txt_margem       = ep_campo(pm, gm, ep_u8("Margem desta peça (%) — vazio = padrão"), "",
                                  ep_u8("Deixe vazio para usar a margem de \"Custos do negócio\"."));
    m_pnl_mais->SetSizer(gm);
    m_pnl_mais->Hide();
    col->Add(m_pnl_mais, 0, wxLEFT | wxTOP | wxBOTTOM, borda);
    m_btn_mais->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        const bool mostrar = !m_pnl_mais->IsShown();
        m_pnl_mais->Show(mostrar);
        m_btn_mais->SetLabel(mostrar ? ep_u8("Menos opções ▲") : ep_u8("Mais opções ▼"));
        relayout();
    });
    col->AddSpacer(borda);

    p->SetSizer(col);

    // Recalcula a cada alteracao.
    for (wxTextCtrl *t : {m_txt_min_mao, m_txt_horas, m_txt_min_preparo, m_txt_embalagem, m_txt_outros, m_txt_frete,
                          m_txt_arquivo, m_txt_arquivo_unid, m_txt_margem})
        t->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { recalcular(); });
    m_spin_unidades->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &) { recalcular(); });
    m_spin_unidades->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { recalcular(); });
    m_ch_impressora->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
        int i = m_ch_impressora->GetSelection();
        if (i >= 0 && i < int(m_dados.impressoras.size())) {
            m_dados.ultima_impressora = m_dados.impressoras[i].nome;
            salvar();
        }
        recalcular();
    });
    m_ch_canal->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
        int i = m_ch_canal->GetSelection();
        if (i >= 0 && i < int(m_dados.canais.size())) {
            m_dados.ultimo_canal = m_dados.canais[i].nome;
            salvar();
        }
        recalcular();
    });

    p->FitInside();
    return p;
}

void EstoquePrecosPanel::ler_fatiamento()
{
    Plater *plater = wxGetApp().plater();
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (plater == nullptr || bundle == nullptr) return;

    PartPlate *plate = plater->get_partplate_list().get_curr_plate();
    const int idx_placa = plater->get_partplate_list().get_curr_plate_index();
    GCodeProcessorResult *res = plate ? plate->get_slice_result() : nullptr;
    if (plate == nullptr || res == nullptr || !plate->is_slice_result_valid()) {
        m_lbl_status->SetLabel(ep_u8("Placa ") + wxString::Format("%d", idx_placa + 1) +
                               ep_u8(" ainda não foi fatiada. Vá em Preparar → Fatiar placa e volte aqui."));
        relayout();
        return;
    }

    // Tempo
    const double segundos = res->print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    m_txt_horas->ChangeValue(ep_fmt(segundos / 3600.0, 3));

    // Impressora selecionada no Bambu Studio
    const Preset &printer = bundle->printers.get_edited_preset();
    std::string perfil_impressora = printer.name;
    if (const ConfigOptionString *modelo = printer.config.option<ConfigOptionString>("printer_model"))
        perfil_impressora += " | " + modelo->value;
    int imp = m_dados.impressora_para_perfil(perfil_impressora);
    if (imp >= 0) m_dados.ultima_impressora = m_dados.impressoras[imp].nome;

    // Nome da peca = nome do projeto, se ainda vazio
    if (m_txt_produto->GetValue().empty())
        m_txt_produto->ChangeValue(plater->get_project_name());

    // Filamentos usados: volume total (modelo + suportes + torre + purga) x densidade
    const ConfigOptionStrings *cores = bundle->project_config.option<ConfigOptionStrings>("filament_colour");
    m_slots.clear();
    for (const auto &kv : res->print_statistics.total_volumes_per_extruder) {
        const size_t id = kv.first;
        const double densidade = id < res->filament_densities.size() ? res->filament_densities[id] : 1.24;
        SlotFilamento s;
        s.id     = int(id);
        s.gramas = kv.second * densidade * 0.001;
        if (s.gramas <= 0.0) continue;
        if (id < bundle->filament_presets.size()) {
            s.perfil = bundle->filament_presets[id];
            if (Preset *fp = bundle->filaments.find_preset(s.perfil)) {
                std::string exibido;
                s.tipo = fp->get_filament_type(exibido);
            }
        }
        if (cores && id < cores->values.size()) s.cor = cores->values[id];
        auto it = m_dados.mapa_filamentos.find(s.perfil + "|" + s.cor);
        if (it != m_dados.mapa_filamentos.end() && m_dados.rolo(it->second) != nullptr) s.rolo_id = it->second;
        m_slots.push_back(s);
    }

    double total_g = 0.0;
    for (const SlotFilamento &s : m_slots) total_g += s.gramas;
    const int h = int(segundos) / 3600, m = (int(segundos) % 3600) / 60;
    m_lbl_status->SetLabel(ep_u8("Placa ") + wxString::Format("%d", idx_placa + 1) + ep_u8(" · ") + ep_u8(printer.name) + ep_u8(" · ") +
                           wxString::Format("%dh%02dmin", h, m) + ep_u8(" · ") + ep_fmt(total_g, 1) + ep_u8(" g"));

    reconstruir_filamentos();
    atualizar_escolhas();
    recalcular();
}

void EstoquePrecosPanel::reconstruir_filamentos()
{
    wxWindow *p = m_pag_preco;
    m_sizer_filamentos->Clear(true);
    if (m_slots.empty()) {
        relayout();
        return;
    }
    for (const wxString &t : {ep_u8("Filamento no Bambu"), ep_u8("Gramas"), ep_u8("Sai de qual rolo do estoque"), ep_u8("R$/g")}) {
        auto *lbl = new wxStaticText(p, wxID_ANY, t);
        lbl->SetForegroundColour(wxColour(110, 110, 110));
        m_sizer_filamentos->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
    }
    for (size_t i = 0; i < m_slots.size(); ++i) {
        SlotFilamento &s = m_slots[i];
        auto *linha = new wxBoxSizer(wxHORIZONTAL);
        auto *amostra = new wxPanel(p, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), wxBORDER_SIMPLE);
        wxColour cor;
        if (s.cor.size() >= 7 && s.cor[0] == '#') cor.Set(ep_u8(s.cor.substr(0, 7)));
        amostra->SetBackgroundColour(cor.IsOk() ? cor : *wxLIGHT_GREY);
        linha->Add(amostra, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        wxString desc = wxString::Format("%d. ", s.id + 1) + ep_u8(s.perfil);
        if (!s.tipo.empty()) desc += ep_u8(" (") + ep_u8(s.tipo) + ep_u8(")");
        linha->Add(new wxStaticText(p, wxID_ANY, desc), 0, wxALIGN_CENTER_VERTICAL);
        m_sizer_filamentos->Add(linha, 0, wxALIGN_CENTER_VERTICAL);

        s.txt_gramas = new wxTextCtrl(p, wxID_ANY, ep_fmt(s.gramas, 2), wxDefaultPosition, wxSize(FromDIP(70), -1));
        m_sizer_filamentos->Add(s.txt_gramas, 0, wxALIGN_CENTER_VERTICAL);
        s.ch_rolo = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(320), -1));
        m_sizer_filamentos->Add(s.ch_rolo, 0);
        s.lbl_custo = new wxStaticText(p, wxID_ANY, "-");
        m_sizer_filamentos->Add(s.lbl_custo, 0, wxALIGN_CENTER_VERTICAL);

        s.txt_gramas->Bind(wxEVT_TEXT, [this, i](wxCommandEvent &) {
            if (i < m_slots.size()) m_slots[i].gramas = ep_num(m_slots[i].txt_gramas->GetValue());
            recalcular();
        });
        s.ch_rolo->Bind(wxEVT_CHOICE, [this, i](wxCommandEvent &) {
            if (i >= m_slots.size()) return;
            SlotFilamento &sl = m_slots[i];
            const int sel = sl.ch_rolo->GetSelection(); // 0 = sem rolo
            sl.rolo_id = (sel >= 1 && sel <= int(m_dados.rolos.size())) ? m_dados.rolos[sel - 1].id : std::string();
            m_dados.mapa_filamentos[sl.perfil + "|" + sl.cor] = sl.rolo_id;
            salvar();
            recalcular();
        });
    }
    relayout();
}

void EstoquePrecosPanel::escolher_insumo()
{
    if (m_dados.insumos.empty()) {
        wxMessageBox(ep_u8("Você ainda não tem insumos cadastrados. Cadastre na aba \"Estoque de insumos\"."), ep_titulo(),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxArrayString nomes;
    for (const Insumo &i : m_dados.insumos)
        nomes.Add(ep_u8(i.nome) + ep_u8("   (") + ep_reais(i.custo_unidade()) + ep_u8(" por ") + ep_u8(i.unidade.empty() ? "un" : i.unidade) + ep_u8(")"));
    const int escolha = wxGetSingleChoiceIndex(ep_u8("Qual insumo vai nesta peça?"), ep_u8("Adicionar insumo"), nomes, 0, this);
    if (escolha < 0 || escolha >= int(m_dados.insumos.size())) return;
    const Insumo &ins = m_dados.insumos[escolha];
    const wxString resp = wxGetTextFromUser(ep_u8("Quanto de \"") + ep_u8(ins.nome) + ep_u8("\" vai em cada peça? (em ") +
                                                ep_u8(ins.unidade.empty() ? "un" : ins.unidade) + ep_u8(")"),
                                            ep_u8("Adicionar insumo"), "1", this);
    if (resp.empty()) return;
    bool ok = false;
    const double qtd = ep_num(resp, 0.0, &ok);
    if (!ok || qtd <= 0.0) return;
    LinhaInsumo l;
    l.insumo_id = ins.id;
    l.qtd = qtd;
    m_insumos_produto.push_back(l);
    reconstruir_insumos();
    recalcular();
}

void EstoquePrecosPanel::reconstruir_insumos()
{
    if (m_sizer_insumos == nullptr) return;
    // Remove insumos que nao existem mais no estoque.
    m_insumos_produto.erase(std::remove_if(m_insumos_produto.begin(), m_insumos_produto.end(),
                                           [this](const LinhaInsumo &l) { return m_dados.insumo(l.insumo_id) == nullptr; }),
                            m_insumos_produto.end());
    wxWindow *p = m_pag_preco;
    m_sizer_insumos->Clear(true);
    for (size_t i = 0; i < m_insumos_produto.size(); ++i) {
        LinhaInsumo &l = m_insumos_produto[i];
        const Insumo *ins = m_dados.insumo(l.insumo_id);
        m_sizer_insumos->Add(new wxStaticText(p, wxID_ANY, ep_u8(ins->nome)), 0, wxALIGN_CENTER_VERTICAL);
        auto *txt = new wxTextCtrl(p, wxID_ANY, ep_fmt(l.qtd, 2), wxDefaultPosition, wxSize(FromDIP(60), -1));
        auto *und = new wxBoxSizer(wxHORIZONTAL);
        und->Add(txt, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        und->Add(new wxStaticText(p, wxID_ANY, ep_u8(ins->unidade.empty() ? "un" : ins->unidade)), 0, wxALIGN_CENTER_VERTICAL);
        m_sizer_insumos->Add(und, 0, wxALIGN_CENTER_VERTICAL);
        l.lbl_custo = new wxStaticText(p, wxID_ANY, ep_reais(ins->custo_unidade() * l.qtd));
        m_sizer_insumos->Add(l.lbl_custo, 0, wxALIGN_CENTER_VERTICAL);
        auto *btn_x = new wxButton(p, wxID_ANY, ep_u8("Tirar"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_sizer_insumos->Add(btn_x, 0, wxALIGN_CENTER_VERTICAL);

        txt->Bind(wxEVT_TEXT, [this, i, txt](wxCommandEvent &) {
            if (i >= m_insumos_produto.size()) return;
            m_insumos_produto[i].qtd = std::max(0.0, ep_num(txt->GetValue()));
            recalcular();
        });
        btn_x->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent &) {
            if (i >= m_insumos_produto.size()) return;
            m_insumos_produto.erase(m_insumos_produto.begin() + i);
            // Recria as linhas depois do evento (o botao clicado sera destruido).
            CallAfter([this]() {
                reconstruir_insumos();
                recalcular();
            });
        });
    }
    relayout();
}

void EstoquePrecosPanel::atualizar_escolhas()
{
    if (m_ch_impressora == nullptr) return;
    m_ch_impressora->Clear();
    int sel_imp = 0;
    for (size_t i = 0; i < m_dados.impressoras.size(); ++i) {
        m_ch_impressora->Append(ep_u8(m_dados.impressoras[i].nome));
        if (m_dados.impressoras[i].nome == m_dados.ultima_impressora) sel_imp = int(i);
    }
    if (!m_dados.impressoras.empty()) m_ch_impressora->SetSelection(sel_imp);

    m_ch_canal->Clear();
    int sel_canal = 0;
    for (size_t i = 0; i < m_dados.canais.size(); ++i) {
        m_ch_canal->Append(ep_u8(m_dados.canais[i].nome));
        if (m_dados.canais[i].nome == m_dados.ultimo_canal) sel_canal = int(i);
    }
    if (!m_dados.canais.empty()) m_ch_canal->SetSelection(sel_canal);

    for (SlotFilamento &s : m_slots) {
        if (s.ch_rolo == nullptr) continue;
        s.ch_rolo->Clear();
        s.ch_rolo->Append(ep_u8("(escolha o rolo)"));
        int sel = 0;
        for (size_t i = 0; i < m_dados.rolos.size(); ++i) {
            const Rolo &r = m_dados.rolos[i];
            s.ch_rolo->Append(ep_u8(r.nome) + ep_u8("  — tem ") + ep_fmt(r.saldo_g, 0) + ep_u8(" g"));
            if (r.id == s.rolo_id) sel = int(i) + 1;
        }
        if (sel == 0) s.rolo_id.clear();
        s.ch_rolo->SetSelection(sel);
    }
    reconstruir_insumos();
}

void EstoquePrecosPanel::recalcular()
{
    if (m_montando || m_destruindo || m_lbl_preco == nullptr) return;

    const int ii = m_ch_impressora->GetSelection();
    const int ic = m_ch_canal->GetSelection();
    if (ii < 0 || ii >= int(m_dados.impressoras.size()) || ic < 0 || ic >= int(m_dados.canais.size())) {
        m_lbl_preco->SetLabel(ep_u8("R$ —"));
        m_lbl_avisos->SetLabel(ep_u8("Cadastre uma impressora e um canal de venda."));
        relayout();
        return;
    }
    const Impressora &imp = m_dados.impressoras[ii];
    const Canal &canal = m_dados.canais[ic];

    Trabalho t;
    std::vector<wxString> avisos;
    for (const SlotFilamento &s : m_slots) {
        const Rolo *r = m_dados.rolo(s.rolo_id);
        t.filamentos.push_back({s.gramas, r ? r->custo_por_grama() : 0.0});
        if (s.lbl_custo) s.lbl_custo->SetLabel(r ? ep_u8("R$ ") + ep_fmt(r->custo_por_grama(), 4) : ep_u8("—"));
        if (r == nullptr)
            avisos.push_back(ep_u8("Escolha o rolo do filamento ") + wxString::Format("%d", s.id + 1) + ep_u8(" (sem rolo, o material conta como R$ 0)."));
        else if (r->custo_por_grama() <= 0.0)
            avisos.push_back(ep_u8("O rolo \"") + ep_u8(r->nome) + ep_u8("\" está sem preço ou peso."));
    }
    if (m_slots.empty())
        avisos.push_back(ep_u8("Fatie a placa em Preparar para trazer o tempo e as gramas."));
    t.horas_impressao       = ep_num(m_txt_horas->GetValue());
    t.unidades_por_placa    = std::max(1, m_spin_unidades->GetValue());
    t.min_preparo_por_placa = ep_num(m_txt_min_preparo->GetValue());
    t.min_mao_obra_por_unid = ep_num(m_txt_min_mao->GetValue());
    double insumos = 0.0;
    for (LinhaInsumo &l : m_insumos_produto)
        if (const Insumo *ins = m_dados.insumo(l.insumo_id)) {
            insumos += ins->custo_unidade() * l.qtd;
            if (l.lbl_custo) l.lbl_custo->SetLabel(ep_reais(ins->custo_unidade() * l.qtd));
        }
    t.insumos_por_unid   = insumos;
    t.embalagem_por_unid = ep_num(m_txt_embalagem->GetValue());
    t.outros_por_unid    = ep_num(m_txt_outros->GetValue());
    t.frete_vendedor     = ep_num(m_txt_frete->GetValue());
    t.arquivo_custo      = ep_num(m_txt_arquivo->GetValue());
    t.arquivo_unidades   = ep_num(m_txt_arquivo_unid->GetValue());
    bool margem_ok = false;
    const double margem = ep_num(m_txt_margem->GetValue(), 0.0, &margem_ok);
    t.margem = margem_ok ? margem / 100.0 : -1.0;

    const ConfigNegocio &cfg = m_dados.config;
    const Resultado r = calcular(t, imp, canal, cfg);
    m_ultimo_preco = r.preco;

    if (!r.valido) {
        m_lbl_preco->SetLabel(ep_u8("REVISE"));
        m_lbl_minimo->SetLabel(ep_u8("As porcentagens (taxas + margem) somam 100% ou mais."));
    } else {
        m_lbl_preco->SetLabel(ep_reais(r.preco));
        m_lbl_minimo->SetLabel(ep_u8("Abaixo de ") + ep_reais(r.preco_minimo) + ep_u8(" você tem prejuízo."));
    }
    m_lbl_r_custo->SetLabel(ep_reais(r.custo_producao));
    m_lbl_r_taxas->SetLabel(ep_reais(r.impostos + r.comissao + r.taxa_pagamento + r.taxa_fixa + r.frete));
    m_lbl_r_reservas->SetLabel(ep_reais(r.publicidade + r.devolucoes + r.cupons));
    m_lbl_r_lucro->SetLabel(ep_reais(r.lucro) + ep_u8("  (") + ep_pct(r.margem_real) + ep_u8(")"));
    m_lbl_r_preco->SetLabel(ep_reais(r.preco));

    wxString aviso_txt;
    for (const wxString &a : avisos) aviso_txt += ep_u8("⚠ ") + a + "\n";
    if (!aviso_txt.empty()) aviso_txt.RemoveLast();
    m_lbl_avisos->SetLabel(aviso_txt);
    m_lbl_avisos->Show(!aviso_txt.empty());

    // Detalhes (texto monoespacado)
    wxString o;
    auto linha = [&o](const wxString &nome, double valor, double preco) {
        wxString n = nome;
        n.Pad(std::max<int>(0, 40 - int(nome.length())));
        wxString v = ep_reais(valor);
        v.Pad(std::max<int>(0, 14 - int(v.length())), ' ', false);
        o += n + v + ep_u8("   ") + (preco > 0.0 ? ep_pct(valor / preco) : wxString()) + "\n";
    };
    o += ep_u8("COMPOSIÇÃO DO PREÇO (por peça)\n");
    o += ep_u8("────────────────────────────────────────────────────────────\n");
    linha(ep_u8("Material (filamento + desperdício)"), r.material, r.preco);
    linha(ep_u8("Energia elétrica"), r.energia, r.preco);
    linha(ep_u8("Depreciação da impressora"), r.depreciacao, r.preco);
    linha(ep_u8("Manutenção e desgaste"), r.manutencao, r.preco);
    linha(ep_u8("Reserva para falhas"), r.falhas, r.preco);
    linha(ep_u8("Custos fixos rateados"), r.fixos, r.preco);
    linha(ep_u8("Mão de obra"), r.mao_de_obra, r.preco);
    linha(ep_u8("Insumos"), r.insumos, r.preco);
    linha(ep_u8("Embalagem avulsa"), r.embalagem, r.preco);
    linha(ep_u8("Arquivo 3D + outros"), r.arquivo + r.outros, r.preco);
    linha(ep_u8("= CUSTO PARA PRODUZIR"), r.custo_producao, r.preco);
    linha(ep_u8("Frete pago por você"), r.frete, r.preco);
    linha(ep_u8("Taxa fixa do canal"), r.taxa_fixa, r.preco);
    linha(ep_u8("Impostos"), r.impostos, r.preco);
    linha(ep_u8("Comissão do canal"), r.comissao, r.preco);
    linha(ep_u8("Taxa de pagamento"), r.taxa_pagamento, r.preco);
    linha(ep_u8("Publicidade"), r.publicidade, r.preco);
    linha(ep_u8("Devoluções e garantia"), r.devolucoes, r.preco);
    linha(ep_u8("Cupons e descontos"), r.cupons, r.preco);
    linha(ep_u8("LUCRO"), r.lucro, r.preco);
    o += ep_u8("────────────────────────────────────────────────────────────\n");
    linha(ep_u8("PREÇO DE VENDA"), r.preco, r.preco);
    o += ep_u8("Hora de trabalho: ") + ep_reais(cfg.custo_hora_trabalho()) + ep_u8("/h  ·  Máquina: ") +
         ep_reais(imp.custo_maquina_por_h(cfg.tarifa_kwh)) + ep_u8("/h  ·  Fixos: ") + ep_reais(cfg.custo_fixo_por_hora_impressao()) + ep_u8("/h\n\n");
    o += ep_u8("MESMA PEÇA EM CADA CANAL\n");
    o += ep_u8("Canal                                        Preço        Lucro\n");
    for (size_t k = 0; k < m_dados.canais.size(); ++k) {
        const Resultado rk = calcular(t, imp, m_dados.canais[k], cfg);
        wxString nome = ep_u8(m_dados.canais[k].nome);
        if (nome.length() > 40) nome = nome.Left(39) + ep_u8("…");
        nome.Pad(std::max<int>(0, 41 - int(nome.length())));
        wxString pr = ep_reais(rk.preco), lu = ep_reais(rk.lucro);
        pr.Pad(std::max<int>(0, 12 - int(pr.length())), ' ', false);
        lu.Pad(std::max<int>(0, 12 - int(lu.length())), ' ', false);
        o += (int(k) == ic ? ep_u8("► ") : ep_u8("  ")) + nome + pr + ep_u8(" ") + lu + "\n";
    }
    m_txt_detalhes->ChangeValue(o);
    relayout();
}

void EstoquePrecosPanel::confirmar_producao()
{
    const int placas = std::max(1, m_spin_placas->GetValue());
    const int unid = std::max(1, m_spin_unidades->GetValue());
    std::vector<ConsumoRolo> rolos;
    std::vector<ConsumoInsumo> insumos;
    wxString resumo;
    for (const SlotFilamento &s : m_slots) {
        const Rolo *r = m_dados.rolo(s.rolo_id);
        if (r == nullptr) {
            resumo += ep_u8(" • Filamento ") + wxString::Format("%d", s.id + 1) + ep_u8(" (") + ep_u8(s.perfil) + ep_u8("): SEM ROLO — não será descontado\n");
            continue;
        }
        rolos.push_back({r->id, s.gramas});
        resumo += ep_u8(" • ") + ep_u8(r->nome) + ep_u8(": −") + ep_fmt(s.gramas * placas, 1) + ep_u8(" g (fica ") + ep_fmt(r->saldo_g - s.gramas * placas, 0) + ep_u8(" g)\n");
    }
    for (const LinhaInsumo &l : m_insumos_produto) {
        const Insumo *ins = m_dados.insumo(l.insumo_id);
        if (ins == nullptr || l.qtd <= 0.0) continue;
        insumos.push_back({ins->id, l.qtd});
        const double q = l.qtd * placas * unid;
        resumo += ep_u8(" • ") + ep_u8(ins->nome) + ep_u8(": −") + ep_fmt(q, 2) + ep_u8(" ") + ep_u8(ins->unidade) + ep_u8(" (fica ") + ep_fmt(ins->saldo - q, 2) + ep_u8(")\n");
    }
    if (rolos.empty() && insumos.empty()) {
        wxMessageBox(ep_u8("Nada para descontar. Escolha o rolo de cada filamento (e os insumos, se houver)."), ep_titulo(),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxString produto = m_txt_produto->GetValue();
    if (produto.empty()) produto = ep_u8("(sem nome)");
    const wxString msg = ep_u8("Confirmar a produção de ") + wxString::Format("%d", placas) + ep_u8(" placa(s) = ") +
                         wxString::Format("%d", placas * unid) + ep_u8(" peça(s) de \"") + produto + ep_u8("\"?\n\n") + resumo;
    if (wxMessageBox(msg, ep_u8("Confirmar produção"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;

    std::vector<std::string> avisos = m_dados.confirmar_producao(ep_std8(produto), placas, unid, rolos, insumos, m_ultimo_preco, ep_agora());
    salvar();
    estoque_alterado();
    atualizar_escolhas();
    recalcular();

    wxString fim = ep_u8("Estoque atualizado.");
    if (!avisos.empty()) {
        fim += ep_u8("\n\n");
        for (const std::string &a : avisos) fim += ep_u8("• ") + ep_u8(a) + "\n";
    }
    wxMessageBox(fim, ep_titulo(), wxOK | (avisos.empty() ? wxICON_INFORMATION : wxICON_WARNING), this);
}

// ---------------------------------------------------------------- tabelas

using Tipo = TabelaEstoque::Tipo;

TabelaEstoque *EstoquePrecosPanel::criar_tabela_rolos(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    auto R = [this](int i) -> Rolo & { return m_dados.rolos[i]; };
    c.push_back({ep_u8("Filamento"), Tipo::Texto, 320, [=](int i) { return ep_u8(R(i).nome); }, [=](int i, const wxString &v) { R(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Material"), Tipo::Texto, 90, [=](int i) { return ep_u8(R(i).material); }, [=](int i, const wxString &v) { R(i).material = ep_std8(v); }});
    c.push_back({ep_u8("Cor"), Tipo::Texto, 90, [=](int i) { return ep_u8(R(i).cor); }, [=](int i, const wxString &v) { R(i).cor = ep_std8(v); }});
    c.push_back({ep_u8("Quanto tem (g)"), Tipo::Numero, 105, [=](int i) { return ep_fmt(R(i).saldo_g, 0); }, [=](int i, const wxString &v) { R(i).saldo_g = ep_num(v); }});
    c.push_back({ep_u8("Preço do rolo (R$)"), Tipo::Numero, 110, [=](int i) { return ep_fmt(R(i).preco_pago); }, [=](int i, const wxString &v) { R(i).preco_pago = ep_num(v); }});
    c.push_back({ep_u8("Peso do rolo (g)"), Tipo::Numero, 100, [=](int i) { return ep_fmt(R(i).peso_g, 0); }, [=](int i, const wxString &v) { R(i).peso_g = ep_num(v, 1000.0); }});
    c.push_back({ep_u8("Avisar abaixo de (g)"), Tipo::Numero, 120, [=](int i) { return ep_fmt(R(i).minimo_g, 0); }, [=](int i, const wxString &v) { R(i).minimo_g = ep_num(v); }});
    c.push_back({ep_u8("R$/g"), Tipo::Calculado, 70, [=](int i) { return ep_fmt(R(i).custo_por_grama(), 4); }, nullptr});

    std::vector<TabelaEstoque::Acao> acoes;
    acoes.push_back({ep_u8("Registrar compra"), [this](int i) {
        Rolo &r = m_dados.rolos[i];
        const wxString qtd = wxGetTextFromUser(ep_u8("Quantos rolos novos de \"") + ep_u8(r.nome) + ep_u8("\" você comprou?"),
                                               ep_u8("Registrar compra"), "1", this);
        bool ok = false;
        const double n = ep_num(qtd, 0.0, &ok);
        if (!ok || n <= 0.0) return;
        const wxString preco = wxGetTextFromUser(ep_u8("Quanto pagou em cada rolo (R$)?"), ep_u8("Registrar compra"), ep_fmt(r.preco_pago), this);
        const double pr = ep_num(preco, 0.0);
        m_dados.registrar_compra_rolo(r.id, n, pr, ep_agora());
    }});
    acoes.push_back({ep_u8("Ajustar quanto tem"), [this](int i) {
        Rolo &r = m_dados.rolos[i];
        const wxString v = wxGetTextFromUser(ep_u8("Quantos gramas de \"") + ep_u8(r.nome) + ep_u8("\" você tem agora?\n(Dica: pese o rolo e desconte o carretel vazio, ~200–250 g.)"),
                                             ep_u8("Ajustar quanto tem"), ep_fmt(r.saldo_g, 0), this);
        bool ok = false;
        const double g = ep_num(v, 0.0, &ok);
        if (!ok) return;
        m_dados.ajustar_saldo_rolo(r.id, g, ep_agora());
    }});

    auto *t = new TabelaEstoque(nb,
        ep_u8("Seus rolos de filamento. Em \"Quanto tem\", informe quantos gramas ainda restam. Ao confirmar uma produção, o que foi usado sai daqui sozinho. "
              "Comprou mais? Clique na linha e em \"Registrar compra\". Vermelho = acabando."),
        std::move(c), [this]() { return int(m_dados.rolos.size()); },
        [this]() {
            Rolo r;
            r.id = m_dados.novo_id("r");
            r.nome = "Novo filamento";
            r.saldo_g = 1000.0;
            r.minimo_g = 100.0;
            m_dados.rolos.push_back(r);
        },
        [this](int i) { m_dados.rolos.erase(m_dados.rolos.begin() + i); },
        [this]() { dados_alterados(); estoque_alterado(); }, false, ep_u8("+ Novo filamento"), std::move(acoes));
    t->set_destaque([this](int i) {
        const Rolo &r = m_dados.rolos[i];
        return r.saldo_g < 0.0 || (r.minimo_g > 0.0 && r.saldo_g < r.minimo_g);
    });
    return t;
}

TabelaEstoque *EstoquePrecosPanel::criar_tabela_insumos(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    auto I = [this](int i) -> Insumo & { return m_dados.insumos[i]; };
    c.push_back({ep_u8("Insumo"), Tipo::Texto, 320, [=](int i) { return ep_u8(I(i).nome); }, [=](int i, const wxString &v) { I(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Unidade (un, ml, g)"), Tipo::Texto, 110, [=](int i) { return ep_u8(I(i).unidade); }, [=](int i, const wxString &v) { I(i).unidade = ep_std8(v); }});
    c.push_back({ep_u8("Quanto tem"), Tipo::Numero, 90, [=](int i) { return ep_fmt(I(i).saldo, 1); }, [=](int i, const wxString &v) { I(i).saldo = ep_num(v); }});
    c.push_back({ep_u8("Preço do pacote (R$)"), Tipo::Numero, 120, [=](int i) { return ep_fmt(I(i).preco_pago); }, [=](int i, const wxString &v) { I(i).preco_pago = ep_num(v); }});
    c.push_back({ep_u8("Vem no pacote"), Tipo::Numero, 95, [=](int i) { return ep_fmt(I(i).quantidade, 1); }, [=](int i, const wxString &v) { I(i).quantidade = ep_num(v, 1.0); }});
    c.push_back({ep_u8("Avisar abaixo de"), Tipo::Numero, 105, [=](int i) { return ep_fmt(I(i).minimo, 1); }, [=](int i, const wxString &v) { I(i).minimo = ep_num(v); }});
    c.push_back({ep_u8("Custo por unidade"), Tipo::Calculado, 110, [=](int i) { return ep_reais(I(i).custo_unidade()); }, nullptr});

    std::vector<TabelaEstoque::Acao> acoes;
    acoes.push_back({ep_u8("Registrar compra"), [this](int i) {
        Insumo &x = m_dados.insumos[i];
        const wxString qtd = wxGetTextFromUser(ep_u8("Quantos pacotes novos de \"") + ep_u8(x.nome) + ep_u8("\" você comprou?"),
                                               ep_u8("Registrar compra"), "1", this);
        bool ok = false;
        const double n = ep_num(qtd, 0.0, &ok);
        if (!ok || n <= 0.0) return;
        const wxString preco = wxGetTextFromUser(ep_u8("Quanto pagou em cada pacote (R$)?"), ep_u8("Registrar compra"), ep_fmt(x.preco_pago), this);
        m_dados.registrar_compra_insumo(x.id, n, ep_num(preco, 0.0), ep_agora());
    }});
    acoes.push_back({ep_u8("Ajustar quanto tem"), [this](int i) {
        Insumo &x = m_dados.insumos[i];
        const wxString v = wxGetTextFromUser(ep_u8("Quanto de \"") + ep_u8(x.nome) + ep_u8("\" você tem agora? (em ") +
                                                 ep_u8(x.unidade.empty() ? "un" : x.unidade) + ep_u8(")"),
                                             ep_u8("Ajustar quanto tem"), ep_fmt(x.saldo, 1), this);
        bool ok = false;
        const double q = ep_num(v, 0.0, &ok);
        if (!ok) return;
        m_dados.ajustar_saldo_insumo(x.id, q, ep_agora());
    }});

    auto *t = new TabelaEstoque(nb,
        ep_u8("Tinta, cola, argolas, ímãs, embalagens... Informe quanto vem no pacote e quanto você ainda tem (ex.: cola em ml). "
              "Ferramentas (alicate, kit de acabamento) não entram aqui: elas já estão nos custos fixos do negócio."),
        std::move(c), [this]() { return int(m_dados.insumos.size()); },
        [this]() {
            Insumo i;
            i.id = m_dados.novo_id("i");
            i.nome = "Novo insumo";
            m_dados.insumos.push_back(i);
        },
        [this](int i) { m_dados.insumos.erase(m_dados.insumos.begin() + i); },
        [this]() { dados_alterados(); estoque_alterado(); }, false, ep_u8("+ Novo insumo"), std::move(acoes));
    t->set_destaque([this](int i) {
        const Insumo &x = m_dados.insumos[i];
        return x.saldo < 0.0 || (x.minimo > 0.0 && x.saldo < x.minimo);
    });
    return t;
}

TabelaEstoque *EstoquePrecosPanel::criar_tabela_impressoras(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    auto P = [this](int i) -> Impressora & { return m_dados.impressoras[i]; };
    c.push_back({ep_u8("Nome"), Tipo::Texto, 220, [=](int i) { return ep_u8(P(i).nome); }, [=](int i, const wxString &v) { P(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Modelo no Bambu (ex.: A1, P1S)"), Tipo::Texto, 170, [=](int i) { return ep_u8(P(i).perfil_bambu); }, [=](int i, const wxString &v) { P(i).perfil_bambu = ep_std8(v); }});
    c.push_back({ep_u8("Preço pago (R$)"), Tipo::Numero, 105, [=](int i) { return ep_fmt(P(i).preco_compra); }, [=](int i, const wxString &v) { P(i).preco_compra = ep_num(v); }});
    c.push_back({ep_u8("Revenda depois (%)"), Tipo::Percentual, 110, [=](int i) { return ep_fmt(P(i).valor_residual * 100.0, 1); }, [=](int i, const wxString &v) { P(i).valor_residual = ep_num(v) / 100.0; }});
    c.push_back({ep_u8("Vida útil (h)"), Tipo::Numero, 85, [=](int i) { return ep_fmt(P(i).vida_util_h, 0); }, [=](int i, const wxString &v) { P(i).vida_util_h = ep_num(v); }});
    c.push_back({ep_u8("Potência (W)"), Tipo::Numero, 85, [=](int i) { return ep_fmt(P(i).potencia_w, 0); }, [=](int i, const wxString &v) { P(i).potencia_w = ep_num(v); }});
    c.push_back({ep_u8("Manutenção (R$/h)"), Tipo::Numero, 110, [=](int i) { return ep_fmt(P(i).manutencao_por_h, 2); }, [=](int i, const wxString &v) { P(i).manutencao_por_h = ep_num(v); }});
    c.push_back({ep_u8("Custo da máquina (R$/h)"), Tipo::Calculado, 140, [=](int i) { return ep_fmt(P(i).custo_maquina_por_h(m_dados.config.tarifa_kwh), 4); }, nullptr});

    return new TabelaEstoque(nb,
        ep_u8("Em \"Modelo no Bambu\", escreva como a impressora aparece no Bambu Studio (A1, A1 mini, P1S, X1...). "
              "Assim a impressora certa é escolhida sozinha ao ler a placa."),
        std::move(c), [this]() { return int(m_dados.impressoras.size()); },
        [this]() {
            Impressora i;
            i.nome = "Nova impressora";
            m_dados.impressoras.push_back(i);
        },
        [this](int i) { m_dados.impressoras.erase(m_dados.impressoras.begin() + i); },
        [this]() { dados_alterados(); }, false, ep_u8("+ Nova impressora"));
}

TabelaEstoque *EstoquePrecosPanel::criar_tabela_canais(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    auto C = [this](int i) -> Canal & { return m_dados.canais[i]; };
    c.push_back({ep_u8("Canal"), Tipo::Texto, 320, [=](int i) { return ep_u8(C(i).nome); }, [=](int i, const wxString &v) { C(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Comissão (%)"), Tipo::Percentual, 100, [=](int i) { return ep_fmt(C(i).comissao * 100.0, 1); }, [=](int i, const wxString &v) { C(i).comissao = ep_num(v) / 100.0; }});
    c.push_back({ep_u8("Taxa fixa por item (R$)"), Tipo::Numero, 140, [=](int i) { return ep_fmt(C(i).taxa_fixa); }, [=](int i, const wxString &v) { C(i).taxa_fixa = ep_num(v); }});
    c.push_back({ep_u8("Taxa de pagamento (%)"), Tipo::Percentual, 140, [=](int i) { return ep_fmt(C(i).taxa_pagamento * 100.0, 1); }, [=](int i, const wxString &v) { C(i).taxa_pagamento = ep_num(v) / 100.0; }});

    return new TabelaEstoque(nb,
        ep_u8("Comissões e taxas mudam com frequência. Os valores iniciais são estimativas — confira sempre na central do vendedor."),
        std::move(c), [this]() { return int(m_dados.canais.size()); },
        [this]() {
            Canal k;
            k.nome = "Novo canal";
            m_dados.canais.push_back(k);
        },
        [this](int i) { m_dados.canais.erase(m_dados.canais.begin() + i); },
        [this]() { dados_alterados(); }, false, ep_u8("+ Novo canal"));
}

TabelaEstoque *EstoquePrecosPanel::criar_tabela_historico(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    // Mais recente primeiro.
    auto H = [this](int i) -> const Movimento & { return m_dados.historico[m_dados.historico.size() - 1 - size_t(i)]; };
    c.push_back({ep_u8("Data"), Tipo::Texto, 120, [=](int i) { return ep_u8(H(i).data); }, nullptr});
    c.push_back({ep_u8("Tipo"), Tipo::Texto, 80, [=](int i) { return ep_u8(H(i).tipo); }, nullptr});
    c.push_back({ep_u8("Peça / motivo"), Tipo::Texto, 260, [=](int i) { return ep_u8(H(i).descricao); }, nullptr});
    c.push_back({ep_u8("Item"), Tipo::Texto, 260, [=](int i) { return ep_u8(H(i).item); }, nullptr});
    c.push_back({ep_u8("Quantidade"), Tipo::Numero, 90, [=](int i) { return ep_fmt(H(i).quantidade, 2); }, nullptr});
    c.push_back({ep_u8("Unidade"), Tipo::Texto, 60, [=](int i) { return ep_u8(H(i).unidade); }, nullptr});
    c.push_back({ep_u8("Preço (R$)"), Tipo::Numero, 90, [=](int i) { return ep_fmt(H(i).valor); }, nullptr});
    return new TabelaEstoque(nb, ep_u8("Tudo que entrou e saiu do estoque: produções, compras e ajustes."), std::move(c),
                             [this]() { return int(m_dados.historico.size()); }, nullptr, nullptr, nullptr, true);
}

// ---------------------------------------------------------------- pagina Custos do negocio

wxWindow *EstoquePrecosPanel::criar_pagina_config(wxNotebook *nb)
{
    auto *p = new wxScrolledWindow(nb, wxID_ANY);
    p->SetBackgroundColour(*wxWHITE);
    p->SetScrollRate(0, FromDIP(12));
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *info = new wxStaticText(p, wxID_ANY, wxEmptyString);

    auto atualizar_info = [this, info]() {
        const ConfigNegocio &c = m_dados.config;
        info->SetLabel(ep_u8("Hora de trabalho: ") + ep_reais(c.custo_hora_trabalho()) + ep_u8("/h   ·   Custos fixos considerados: ") +
                       ep_reais(c.custos_fixos_considerados()) + ep_u8("/mês   ·   Custo fixo por hora de impressão: ") +
                       ep_reais(c.custo_fixo_por_hora_impressao()) + ep_u8("/h"));
    };

    struct Campo { const char *secao; const char *rotulo; double *valor; bool percentual; const char *dica; };
    ConfigNegocio &c = m_dados.config;
    const Campo campos[] = {
        {"Energia", "Tarifa de energia (R$/kWh) — com impostos e bandeira", &c.tarifa_kwh, false, "Valor total da conta ÷ kWh consumidos."},
        {"Mão de obra", "Retirada mensal desejada (R$)", &c.retirada_mensal, false, "Quanto você quer receber por mês pelo seu trabalho."},
        {nullptr, "Horas dedicadas ao negócio por mês", &c.horas_trabalho_mes, false, ""},
        {"Custos fixos mensais", "Total de custos fixos por mês (R$) — aluguel, internet, software, contador, ferramentas...", &c.custos_fixos_mensais, false, "Não inclua o DAS-MEI aqui; ele tem campo próprio."},
        {nullptr, "DAS-MEI (R$/mês) — só entra se o regime for MEI", &c.das_mei, false, "Confira o valor vigente no PGMEI."},
        {nullptr, "Horas de impressão produtivas por mês", &c.horas_impressao_mes, false, "Horas em que as impressoras imprimem produtos vendáveis."},
        {"Perdas", "Desperdício de material — purga extra, borda, ponta de rolo (%)", &c.desperdicio, true, ""},
        {nullptr, "Reserva para falhas e reimpressões (% sobre material + máquina)", &c.reserva_falhas, true, ""},
        {"Impostos e venda", "Alíquota de imposto sobre cada venda (%) — MEI = 0", &c.aliquota_imposto, true, "Simples Nacional 1ª faixa: Anexo I 4%, Anexo II 4,5%. Confirme com seu contador."},
        {nullptr, "Margem de lucro líquida padrão (% do preço)", &c.margem_padrao, true, "Lucro que sobra depois de pagar todos os custos, taxas e impostos."},
        {nullptr, "Publicidade / anúncios (% do preço)", &c.publicidade, true, ""},
        {nullptr, "Reserva para devoluções, trocas e garantia (% do preço)", &c.devolucoes, true, ""},
        {nullptr, "Reserva para cupons e descontos (% do preço)", &c.cupons, true, ""},
    };

    wxFlexGridSizer *g = nullptr;
    for (const Campo &cp : campos) {
        if (cp.secao != nullptr) {
            auto *box = new wxStaticBoxSizer(wxVERTICAL, p, ep_u8(cp.secao));
            g = new wxFlexGridSizer(2, FromDIP(4), FromDIP(10));
            g->AddGrowableCol(0);
            box->Add(g, 0, wxEXPAND | wxALL, FromDIP(4));
            sizer->Add(box, 0, wxEXPAND | wxALL, FromDIP(6));
        }
        g->Add(new wxStaticText(p, wxID_ANY, ep_u8(cp.rotulo)), 0, wxALIGN_CENTER_VERTICAL);
        auto *txt = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(110), -1));
        if (cp.dica[0] != '\0') txt->SetToolTip(ep_u8(cp.dica));
        g->Add(txt, 0);
        double *valor = cp.valor;
        const bool percentual = cp.percentual;
        m_cfg_recarregar.push_back([txt, valor, percentual]() { txt->ChangeValue(ep_fmt(percentual ? *valor * 100.0 : *valor, percentual ? 1 : 2)); });
        txt->Bind(wxEVT_TEXT, [this, txt, valor, percentual, atualizar_info](wxCommandEvent &) {
            bool ok = false;
            const double v = ep_num(txt->GetValue(), 0.0, &ok);
            if (!ok) return;
            *valor = percentual ? v / 100.0 : v;
            atualizar_info();
            if (m_tab_impressoras) m_tab_impressoras->recarregar();
            dados_alterados();
        });
    }

    auto *box_op = new wxStaticBoxSizer(wxVERTICAL, p, ep_u8("Opções"));
    auto *chk_mei = new wxCheckBox(p, wxID_ANY, ep_u8("Regime MEI (soma o DAS-MEI aos custos fixos)"));
    auto *chk_90 = new wxCheckBox(p, wxID_ANY, ep_u8("Arredondar o preço final para terminar em ,90 (ex.: 41,37 → 41,90)"));
    box_op->Add(chk_mei, 0, wxALL, FromDIP(4));
    box_op->Add(chk_90, 0, wxALL, FromDIP(4));
    sizer->Add(box_op, 0, wxEXPAND | wxALL, FromDIP(6));
    m_cfg_recarregar.push_back([this, chk_mei, chk_90]() {
        chk_mei->SetValue(m_dados.config.regime_mei);
        chk_90->SetValue(m_dados.config.arredondar_90);
    });
    chk_mei->Bind(wxEVT_CHECKBOX, [this, chk_mei, atualizar_info](wxCommandEvent &) {
        m_dados.config.regime_mei = chk_mei->GetValue();
        atualizar_info();
        dados_alterados();
    });
    chk_90->Bind(wxEVT_CHECKBOX, [this, chk_90](wxCommandEvent &) {
        m_dados.config.arredondar_90 = chk_90->GetValue();
        dados_alterados();
    });

    sizer->Add(info, 0, wxALL, FromDIP(10));
    auto *arquivo = new wxStaticText(p, wxID_ANY, ep_u8("Dados salvos em: ") + ep_u8(caminho_arquivo()));
    arquivo->SetForegroundColour(wxColour(110, 110, 110));
    sizer->Add(arquivo, 0, wxLEFT | wxBOTTOM, FromDIP(10));

    for (auto &f : m_cfg_recarregar) f();
    atualizar_info();
    p->SetSizer(sizer);
    p->FitInside();
    return p;
}

}} // namespace Slic3r::GUI
