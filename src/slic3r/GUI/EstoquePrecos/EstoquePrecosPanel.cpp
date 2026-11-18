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

// ---------------------------------------------------------------- TabelaEstoque

TabelaEstoque::TabelaEstoque(wxWindow *parent, const wxString &ajuda, std::vector<Coluna> colunas,
                             std::function<int()> contar, std::function<void()> adicionar, std::function<void(int)> remover,
                             std::function<void()> ao_alterar, bool somente_leitura)
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
    sizer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    if (!somente_leitura) {
        auto *botoes = new wxBoxSizer(wxHORIZONTAL);
        auto *btn_add = new wxButton(this, wxID_ANY, ep_u8("Adicionar linha"));
        auto *btn_del = new wxButton(this, wxID_ANY, ep_u8("Remover linha selecionada"));
        botoes->Add(btn_add, 0, wxRIGHT, FromDIP(8));
        botoes->Add(btn_del, 0);
        sizer->Add(botoes, 0, wxALL, FromDIP(8));

        btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            if (!m_adicionar) return;
            m_adicionar();
            if (m_ao_alterar) m_ao_alterar();
            recarregar();
            int ultima = m_grid->GetNumberRows() - 1;
            if (ultima >= 0) {
                m_grid->SetGridCursor(ultima, 0);
                m_grid->MakeCellVisible(ultima, 0);
            }
        });
        btn_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            if (!m_remover) return;
            wxArrayInt linhas = m_grid->GetSelectedRows();
            int linha = linhas.IsEmpty() ? m_grid->GetGridCursorRow() : linhas[0];
            if (linha < 0 || linha >= m_contar()) return;
            wxString nome = m_grid->GetCellValue(linha, 0);
            if (wxMessageBox(ep_u8("Remover \"") + nome + ep_u8("\"?"), ep_u8("Estoque e Preços"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
                return;
            m_remover(linha);
            if (m_ao_alterar) m_ao_alterar();
            recarregar();
        });
    }

    SetSizer(sizer);
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
    m_notebook->AddPage(criar_pagina_precificar(m_notebook), ep_u8("Precificar placa"), true);
    m_tab_rolos = criar_tabela_rolos(m_notebook);
    m_notebook->AddPage(m_tab_rolos, ep_u8("Filamentos (estoque)"));
    m_tab_insumos = criar_tabela_insumos(m_notebook);
    m_notebook->AddPage(m_tab_insumos, ep_u8("Insumos (estoque)"));
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
                     ep_u8("Estoque e Preços"), wxOK | wxICON_WARNING);
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

void EstoquePrecosPanel::on_activate()
{
    if (m_montando || m_destruindo) return;
    ler_fatiamento();
}

// ---------------------------------------------------------------- pagina Precificar

static wxTextCtrl *ep_campo(wxWindow *parent, wxFlexGridSizer *grid, const wxString &rotulo, const wxString &valor, const wxString &dica = wxString())
{
    grid->Add(new wxStaticText(parent, wxID_ANY, rotulo), 0, wxALIGN_CENTER_VERTICAL);
    auto *txt = new wxTextCtrl(parent, wxID_ANY, valor, wxDefaultPosition, wxSize(parent->FromDIP(110), -1));
    if (!dica.empty()) txt->SetToolTip(dica);
    grid->Add(txt, 0, wxEXPAND);
    return txt;
}

wxWindow *EstoquePrecosPanel::criar_pagina_precificar(wxNotebook *nb)
{
    m_pag_preco = new wxScrolledWindow(nb, wxID_ANY);
    m_pag_preco->SetBackgroundColour(*wxWHITE);
    m_pag_preco->SetScrollRate(0, FromDIP(12));
    wxWindow *p = m_pag_preco;

    auto *principal = new wxBoxSizer(wxHORIZONTAL);
    auto *esq = new wxBoxSizer(wxVERTICAL);
    auto *dir = new wxBoxSizer(wxVERTICAL);

    // --- Fatiamento
    auto *box_fat = new wxStaticBoxSizer(wxVERTICAL, p, ep_u8("1. Dados do fatiamento (placa atual)"));
    auto *btn_ler = new wxButton(p, wxID_ANY, ep_u8("Ler placa fatiada"));
    btn_ler->SetToolTip(ep_u8("Fatie a placa em Preparar e depois clique aqui. Também é lido sempre que você abre esta aba."));
    box_fat->Add(btn_ler, 0, wxALL, FromDIP(4));
    m_lbl_status = new wxStaticText(p, wxID_ANY, ep_u8("Fatie a placa e clique em \"Ler placa fatiada\"."));
    box_fat->Add(m_lbl_status, 0, wxALL, FromDIP(4));
    m_sizer_filamentos = new wxFlexGridSizer(4, FromDIP(4), FromDIP(8));
    m_sizer_filamentos->AddGrowableCol(2);
    box_fat->Add(m_sizer_filamentos, 0, wxEXPAND | wxALL, FromDIP(4));
    esq->Add(box_fat, 0, wxEXPAND | wxALL, FromDIP(6));
    btn_ler->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ler_fatiamento(); });

    // --- Produto
    auto *box_prod = new wxStaticBoxSizer(wxVERTICAL, p, ep_u8("2. Produto e venda"));
    auto *g = new wxFlexGridSizer(2, FromDIP(4), FromDIP(8));
    g->AddGrowableCol(1);
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Nome do produto")), 0, wxALIGN_CENTER_VERTICAL);
    m_txt_produto = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(260), -1));
    g->Add(m_txt_produto, 1, wxEXPAND);
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Impressora")), 0, wxALIGN_CENTER_VERTICAL);
    m_ch_impressora = new wxChoice(p, wxID_ANY);
    g->Add(m_ch_impressora, 1, wxEXPAND);
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Canal de venda")), 0, wxALIGN_CENTER_VERTICAL);
    m_ch_canal = new wxChoice(p, wxID_ANY);
    g->Add(m_ch_canal, 1, wxEXPAND);
    g->Add(new wxStaticText(p, wxID_ANY, ep_u8("Unidades por placa")), 0, wxALIGN_CENTER_VERTICAL);
    m_spin_unidades = new wxSpinCtrl(p, wxID_ANY, "1", wxDefaultPosition, wxSize(FromDIP(110), -1), wxSP_ARROW_KEYS, 1, 10000, 1);
    g->Add(m_spin_unidades, 0);
    m_txt_horas        = ep_campo(p, g, ep_u8("Tempo de impressão da placa (horas)"), "0", ep_u8("Preenchido pelo fatiamento. Pode ajustar."));
    m_txt_min_preparo  = ep_campo(p, g, ep_u8("Minutos de preparo + retirada (por placa)"), "15", ep_u8("Modelagem/fatiamento + retirar a peça e limpar a mesa."));
    m_txt_min_mao      = ep_campo(p, g, ep_u8("Minutos de mão de obra (por unidade)"), "0", ep_u8("Pós-processamento, pintura, montagem, embalagem, atendimento e postagem."));
    m_txt_embalagem    = ep_campo(p, g, ep_u8("Embalagem avulsa (R$ por unidade)"), "0", ep_u8("Use se a embalagem não estiver na lista de insumos abaixo."));
    m_txt_outros       = ep_campo(p, g, ep_u8("Outros custos diretos (R$ por unidade)"), "0");
    m_txt_frete        = ep_campo(p, g, ep_u8("Frete pago por você (R$ por pedido)"), "0");
    m_txt_arquivo      = ep_campo(p, g, ep_u8("Custo do arquivo/modelo 3D (R$)"), "0");
    m_txt_arquivo_unid = ep_campo(p, g, ep_u8("Unidades para pagar o arquivo"), "100");
    m_txt_margem       = ep_campo(p, g, ep_u8("Margem deste produto (%) — vazio = padrão"), "", ep_u8("Deixe vazio para usar a margem de \"Custos do negócio\"."));
    box_prod->Add(g, 0, wxEXPAND | wxALL, FromDIP(4));
    esq->Add(box_prod, 0, wxEXPAND | wxALL, FromDIP(6));

    // --- Insumos do produto
    auto *box_ins = new wxStaticBoxSizer(wxVERTICAL, p, ep_u8("3. Insumos por unidade (tinta, componentes, embalagem)"));
    m_grid_insumos = new wxGrid(p, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(150)));
    m_grid_insumos->CreateGrid(0, 3);
    m_grid_insumos->SetRowLabelSize(0);
    m_grid_insumos->SetColLabelValue(0, ep_u8("Insumo"));
    m_grid_insumos->SetColLabelValue(1, ep_u8("Qtd. por unidade"));
    m_grid_insumos->SetColLabelValue(2, ep_u8("Custo (R$)"));
    m_grid_insumos->SetColSize(0, FromDIP(300));
    m_grid_insumos->SetColSize(1, FromDIP(110));
    m_grid_insumos->SetColSize(2, FromDIP(90));
    m_grid_insumos->Bind(wxEVT_GRID_CELL_CHANGED, &EstoquePrecosPanel::on_insumo_grid_changed, this);
    box_ins->Add(m_grid_insumos, 0, wxEXPAND | wxALL, FromDIP(4));
    auto *bins = new wxBoxSizer(wxHORIZONTAL);
    auto *btn_ins_add = new wxButton(p, wxID_ANY, ep_u8("Adicionar insumo"));
    auto *btn_ins_del = new wxButton(p, wxID_ANY, ep_u8("Remover selecionado"));
    bins->Add(btn_ins_add, 0, wxRIGHT, FromDIP(8));
    bins->Add(btn_ins_del, 0);
    box_ins->Add(bins, 0, wxALL, FromDIP(4));
    esq->Add(box_ins, 0, wxEXPAND | wxALL, FromDIP(6));
    btn_ins_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (m_dados.insumos.empty()) {
            wxMessageBox(ep_u8("Cadastre os insumos na aba \"Insumos (estoque)\" primeiro."), ep_u8("Estoque e Preços"), wxOK | wxICON_INFORMATION, this);
            return;
        }
        m_insumos_produto.push_back({m_dados.insumos.front().id, 1.0});
        recarregar_insumos_produto();
        recalcular();
    });
    btn_ins_del->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        int linha = m_grid_insumos->GetGridCursorRow();
        if (linha < 0 || linha >= int(m_insumos_produto.size())) return;
        m_insumos_produto.erase(m_insumos_produto.begin() + linha);
        recarregar_insumos_produto();
        recalcular();
    });

    // --- Resultado
    auto *box_res = new wxStaticBoxSizer(wxVERTICAL, p, ep_u8("Resultado"));
    m_lbl_preco = new wxStaticText(p, wxID_ANY, ep_u8("Preço sugerido: —"));
    wxFont fonte_preco = m_lbl_preco->GetFont();
    fonte_preco.SetPointSize(fonte_preco.GetPointSize() + 6);
    fonte_preco.SetWeight(wxFONTWEIGHT_BOLD);
    m_lbl_preco->SetFont(fonte_preco);
    m_lbl_preco->SetForegroundColour(wxColour(0, 150, 80));
    box_res->Add(m_lbl_preco, 0, wxALL, FromDIP(6));
    m_txt_resultado = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(520), FromDIP(560)),
                                     wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_txt_resultado->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    box_res->Add(m_txt_resultado, 1, wxEXPAND | wxALL, FromDIP(4));

    auto *box_prodc = new wxStaticBoxSizer(wxHORIZONTAL, p, ep_u8("4. Produção"));
    box_prodc->Add(new wxStaticText(p, wxID_ANY, ep_u8("Placas produzidas")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_spin_placas = new wxSpinCtrl(p, wxID_ANY, "1", wxDefaultPosition, wxSize(FromDIP(80), -1), wxSP_ARROW_KEYS, 1, 1000, 1);
    box_prodc->Add(m_spin_placas, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    auto *btn_confirmar = new wxButton(p, wxID_ANY, ep_u8("Confirmar produção (descontar do estoque)"));
    box_prodc->Add(btn_confirmar, 0, wxALIGN_CENTER_VERTICAL);
    btn_confirmar->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { confirmar_producao(); });

    dir->Add(box_res, 1, wxEXPAND | wxALL, FromDIP(6));
    dir->Add(box_prodc, 0, wxEXPAND | wxALL, FromDIP(6));

    principal->Add(esq, 0, wxEXPAND);
    principal->Add(dir, 1, wxEXPAND);
    p->SetSizer(principal);

    // Recalcula a cada alteracao.
    for (wxTextCtrl *t : {m_txt_horas, m_txt_min_preparo, m_txt_min_mao, m_txt_embalagem, m_txt_outros, m_txt_frete,
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
        m_pag_preco->Layout();
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

    // Nome do produto = nome do projeto, se ainda vazio
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
                           wxString::Format("%dh%02dmin", h, m) + ep_u8(" · ") + ep_fmt(total_g, 1) + ep_u8(" g no total"));

    reconstruir_filamentos();
    atualizar_escolhas();
    recalcular();
}

void EstoquePrecosPanel::reconstruir_filamentos()
{
    wxWindow *p = m_pag_preco;
    m_sizer_filamentos->Clear(true);
    if (m_slots.empty()) {
        m_pag_preco->Layout();
        return;
    }
    for (const wxString &t : {ep_u8("Slot / perfil no Bambu"), ep_u8("Gramas"), ep_u8("Rolo do estoque"), ep_u8("R$/g")}) {
        auto *lbl = new wxStaticText(p, wxID_ANY, t);
        lbl->SetFont(lbl->GetFont().Bold());
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
        s.ch_rolo = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(280), -1));
        m_sizer_filamentos->Add(s.ch_rolo, 1, wxEXPAND);
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
    m_pag_preco->Layout();
    m_pag_preco->FitInside();
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
        s.ch_rolo->Append(ep_u8("(sem rolo — custo zero)"));
        int sel = 0;
        for (size_t i = 0; i < m_dados.rolos.size(); ++i) {
            const Rolo &r = m_dados.rolos[i];
            s.ch_rolo->Append(ep_u8(r.nome) + ep_u8("  [") + ep_fmt(r.saldo_g, 0) + ep_u8(" g]"));
            if (r.id == s.rolo_id) sel = int(i) + 1;
        }
        if (sel == 0) s.rolo_id.clear();
        s.ch_rolo->SetSelection(sel);
    }
    recarregar_insumos_produto();
}

void EstoquePrecosPanel::recarregar_insumos_produto()
{
    if (m_grid_insumos == nullptr) return;
    wxArrayString nomes;
    for (const Insumo &i : m_dados.insumos) nomes.Add(ep_u8(i.nome));
    // Remove insumos que nao existem mais.
    m_insumos_produto.erase(std::remove_if(m_insumos_produto.begin(), m_insumos_produto.end(),
                                           [this](const ItemInsumo &it) { return m_dados.insumo(it.insumo_id) == nullptr; }),
                            m_insumos_produto.end());
    const int n = int(m_insumos_produto.size());
    if (m_grid_insumos->GetNumberRows() < n) m_grid_insumos->AppendRows(n - m_grid_insumos->GetNumberRows());
    if (m_grid_insumos->GetNumberRows() > n) m_grid_insumos->DeleteRows(n, m_grid_insumos->GetNumberRows() - n);
    for (int r = 0; r < n; ++r) {
        const ItemInsumo &it = m_insumos_produto[r];
        const Insumo *ins = m_dados.insumo(it.insumo_id);
        m_grid_insumos->SetCellEditor(r, 0, new wxGridCellChoiceEditor(nomes));
        m_grid_insumos->SetCellValue(r, 0, ins ? ep_u8(ins->nome) : wxString());
        m_grid_insumos->SetCellValue(r, 1, ep_fmt(it.qtd_por_unidade, 3));
        m_grid_insumos->SetCellValue(r, 2, ep_fmt(ins ? ins->custo_unidade() * it.qtd_por_unidade : 0.0, 2));
        m_grid_insumos->SetReadOnly(r, 2, true);
        m_grid_insumos->SetCellBackgroundColour(r, 2, wxColour(242, 242, 242));
    }
    m_grid_insumos->ForceRefresh();
}

void EstoquePrecosPanel::on_insumo_grid_changed(wxGridEvent &evt)
{
    const int r = evt.GetRow();
    if (r < 0 || r >= int(m_insumos_produto.size())) return;
    if (evt.GetCol() == 0) {
        const std::string nome = ep_std8(m_grid_insumos->GetCellValue(r, 0));
        for (const Insumo &i : m_dados.insumos)
            if (i.nome == nome) { m_insumos_produto[r].insumo_id = i.id; break; }
    } else if (evt.GetCol() == 1) {
        m_insumos_produto[r].qtd_por_unidade = std::max(0.0, ep_num(m_grid_insumos->GetCellValue(r, 1)));
    }
    CallAfter([this]() {
        recarregar_insumos_produto();
        recalcular();
    });
}

void EstoquePrecosPanel::recalcular()
{
    if (m_montando || m_destruindo || m_txt_resultado == nullptr) return;

    const int ii = m_ch_impressora->GetSelection();
    const int ic = m_ch_canal->GetSelection();
    if (ii < 0 || ii >= int(m_dados.impressoras.size()) || ic < 0 || ic >= int(m_dados.canais.size())) {
        m_lbl_preco->SetLabel(ep_u8("Cadastre uma impressora e um canal de venda."));
        m_txt_resultado->ChangeValue(wxEmptyString);
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
            avisos.push_back(ep_u8("Filamento ") + wxString::Format("%d", s.id + 1) + ep_u8(" sem rolo escolhido: material entra como R$ 0."));
        else if (r->custo_por_grama() <= 0.0)
            avisos.push_back(ep_u8("Rolo \"") + ep_u8(r->nome) + ep_u8("\" sem preço ou peso: material entra como R$ 0."));
    }
    t.horas_impressao       = ep_num(m_txt_horas->GetValue());
    t.unidades_por_placa    = std::max(1, m_spin_unidades->GetValue());
    t.min_preparo_por_placa = ep_num(m_txt_min_preparo->GetValue());
    t.min_mao_obra_por_unid = ep_num(m_txt_min_mao->GetValue());
    double insumos = 0.0;
    for (const ItemInsumo &it : m_insumos_produto)
        if (const Insumo *ins = m_dados.insumo(it.insumo_id)) insumos += ins->custo_unidade() * it.qtd_por_unidade;
    t.insumos_por_unid   = insumos;
    t.embalagem_por_unid = ep_num(m_txt_embalagem->GetValue());
    t.outros_por_unid    = ep_num(m_txt_outros->GetValue());
    t.frete_vendedor     = ep_num(m_txt_frete->GetValue());
    t.arquivo_custo      = ep_num(m_txt_arquivo->GetValue());
    t.arquivo_unidades   = ep_num(m_txt_arquivo_unid->GetValue());
    bool margem_ok = false;
    const double margem = ep_num(m_txt_margem->GetValue(), 0.0, &margem_ok);
    t.margem = margem_ok ? margem / 100.0 : -1.0;
    if (m_slots.empty())
        avisos.push_back(ep_u8("Nenhum filamento lido: fatie a placa e clique em \"Ler placa fatiada\"."));

    const ConfigNegocio &cfg = m_dados.config;
    const Resultado r = calcular(t, imp, canal, cfg);
    m_ultimo_preco = r.preco;

    if (!r.valido) {
        m_lbl_preco->SetLabel(ep_u8("REVISE: os percentuais somam 100% ou mais"));
    } else {
        m_lbl_preco->SetLabel(ep_u8("Preço sugerido: ") + ep_reais(r.preco) + ep_u8("   (mínimo ") + ep_reais(r.preco_minimo) + ep_u8(")"));
    }

    wxString o;
    auto linha = [&o](const wxString &nome, double valor, double preco) {
        wxString n = nome;
        n.Pad(std::max<int>(0, 40 - int(nome.length())));
        wxString v = ep_reais(valor);
        v.Pad(std::max<int>(0, 14 - int(v.length())), ' ', false);
        o += n + v + ep_u8("   ") + (preco > 0.0 ? ep_pct(valor / preco) : wxString()) + "\n";
    };
    if (!avisos.empty()) {
        o += ep_u8("ATENÇÃO\n");
        for (const wxString &a : avisos) o += ep_u8(" • ") + a + "\n";
        o += "\n";
    }
    o += ep_u8("COMPOSIÇÃO DO PREÇO (por unidade)\n");
    o += ep_u8("────────────────────────────────────────────────────────────\n");
    linha(ep_u8("Material (filamento + desperdício)"), r.material, r.preco);
    linha(ep_u8("Energia elétrica"), r.energia, r.preco);
    linha(ep_u8("Depreciação da impressora"), r.depreciacao, r.preco);
    linha(ep_u8("Manutenção e desgaste"), r.manutencao, r.preco);
    linha(ep_u8("Reserva para falhas"), r.falhas, r.preco);
    linha(ep_u8("Custos fixos rateados"), r.fixos, r.preco);
    linha(ep_u8("Mão de obra"), r.mao_de_obra, r.preco);
    linha(ep_u8("Insumos (tinta, componentes...)"), r.insumos, r.preco);
    linha(ep_u8("Embalagem avulsa"), r.embalagem, r.preco);
    linha(ep_u8("Arquivo/modelo + outros"), r.arquivo + r.outros, r.preco);
    linha(ep_u8("= CUSTO DE PRODUÇÃO"), r.custo_producao, r.preco);
    linha(ep_u8("Frete pago pelo vendedor"), r.frete, r.preco);
    linha(ep_u8("Taxa fixa do canal"), r.taxa_fixa, r.preco);
    linha(ep_u8("Impostos"), r.impostos, r.preco);
    linha(ep_u8("Comissão do canal"), r.comissao, r.preco);
    linha(ep_u8("Taxa de pagamento"), r.taxa_pagamento, r.preco);
    linha(ep_u8("Publicidade"), r.publicidade, r.preco);
    linha(ep_u8("Devoluções e garantia"), r.devolucoes, r.preco);
    linha(ep_u8("Cupons e descontos"), r.cupons, r.preco);
    linha(ep_u8("LUCRO LÍQUIDO"), r.lucro, r.preco);
    o += ep_u8("────────────────────────────────────────────────────────────\n");
    linha(ep_u8("PREÇO DE VENDA"), r.preco, r.preco);
    o += ep_u8("Margem real: ") + ep_pct(r.margem_real) + ep_u8("   ·   Margem pedida: ") + ep_pct(r.margem) + "\n";
    o += ep_u8("Hora de trabalho: ") + ep_reais(cfg.custo_hora_trabalho()) + ep_u8("/h   ·   Máquina: ") +
         ep_reais(imp.custo_maquina_por_h(cfg.tarifa_kwh)) + ep_u8("/h   ·   Fixos: ") + ep_reais(cfg.custo_fixo_por_hora_impressao()) + ep_u8("/h\n\n");

    o += ep_u8("MESMO PRODUTO EM CADA CANAL\n");
    o += ep_u8("Canal                                        Preço        Lucro   Margem\n");
    for (size_t k = 0; k < m_dados.canais.size(); ++k) {
        const Resultado rk = calcular(t, imp, m_dados.canais[k], cfg);
        wxString nome = ep_u8(m_dados.canais[k].nome);
        if (nome.length() > 40) nome = nome.Left(39) + ep_u8("…");
        nome.Pad(std::max<int>(0, 41 - int(nome.length())));
        wxString pr = ep_reais(rk.preco), lu = ep_reais(rk.lucro);
        pr.Pad(std::max<int>(0, 12 - int(pr.length())), ' ', false);
        lu.Pad(std::max<int>(0, 12 - int(lu.length())), ' ', false);
        o += (int(k) == ic ? ep_u8("► ") : ep_u8("  ")) + nome + pr + ep_u8(" ") + lu + ep_u8("   ") + ep_pct(rk.margem_real) + "\n";
    }
    m_txt_resultado->ChangeValue(o);
    m_pag_preco->Layout();
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
    for (const ItemInsumo &it : m_insumos_produto) {
        const Insumo *ins = m_dados.insumo(it.insumo_id);
        if (ins == nullptr || it.qtd_por_unidade <= 0.0) continue;
        insumos.push_back({ins->id, it.qtd_por_unidade});
        const double q = it.qtd_por_unidade * placas * unid;
        resumo += ep_u8(" • ") + ep_u8(ins->nome) + ep_u8(": −") + ep_fmt(q, 2) + ep_u8(" ") + ep_u8(ins->unidade) + ep_u8(" (fica ") + ep_fmt(ins->saldo - q, 2) + ep_u8(")\n");
    }
    if (rolos.empty() && insumos.empty()) {
        wxMessageBox(ep_u8("Nada para descontar. Escolha o rolo de cada filamento (e os insumos, se houver)."), ep_u8("Estoque e Preços"),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxString produto = m_txt_produto->GetValue();
    if (produto.empty()) produto = ep_u8("(sem nome)");
    const wxString msg = ep_u8("Confirmar a produção de ") + wxString::Format("%d", placas) + ep_u8(" placa(s) = ") +
                         wxString::Format("%d", placas * unid) + ep_u8(" unidade(s) de \"") + produto + ep_u8("\"?\n\n") + resumo;
    if (wxMessageBox(msg, ep_u8("Confirmar produção"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;

    std::vector<std::string> avisos = m_dados.confirmar_producao(ep_std8(produto), placas, unid, rolos, insumos, m_ultimo_preco, ep_agora());
    salvar();
    for (TabelaEstoque *t : {m_tab_rolos, m_tab_insumos, m_tab_historico}) t->recarregar();
    atualizar_escolhas();
    recalcular();

    wxString fim = ep_u8("Estoque atualizado.");
    if (!avisos.empty()) {
        fim += ep_u8("\n\n");
        for (const std::string &a : avisos) fim += ep_u8("• ") + ep_u8(a) + "\n";
    }
    wxMessageBox(fim, ep_u8("Estoque e Preços"), wxOK | (avisos.empty() ? wxICON_INFORMATION : wxICON_WARNING), this);
}

// ---------------------------------------------------------------- tabelas

using Tipo = TabelaEstoque::Tipo;

TabelaEstoque *EstoquePrecosPanel::criar_tabela_rolos(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    auto R = [this](int i) -> Rolo & { return m_dados.rolos[i]; };
    c.push_back({ep_u8("Nome do rolo"), Tipo::Texto, 300, [=](int i) { return ep_u8(R(i).nome); }, [=](int i, const wxString &v) { R(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Material"), Tipo::Texto, 80, [=](int i) { return ep_u8(R(i).material); }, [=](int i, const wxString &v) { R(i).material = ep_std8(v); }});
    c.push_back({ep_u8("Marca"), Tipo::Texto, 100, [=](int i) { return ep_u8(R(i).marca); }, [=](int i, const wxString &v) { R(i).marca = ep_std8(v); }});
    c.push_back({ep_u8("Cor"), Tipo::Texto, 80, [=](int i) { return ep_u8(R(i).cor); }, [=](int i, const wxString &v) { R(i).cor = ep_std8(v); }});
    c.push_back({ep_u8("Preço pago (R$)"), Tipo::Numero, 95, [=](int i) { return ep_fmt(R(i).preco_pago); }, [=](int i, const wxString &v) { R(i).preco_pago = ep_num(v); }});
    c.push_back({ep_u8("Peso do rolo (g)"), Tipo::Numero, 95, [=](int i) { return ep_fmt(R(i).peso_g, 0); }, [=](int i, const wxString &v) { R(i).peso_g = ep_num(v, 1000.0); }});
    c.push_back({ep_u8("Saldo (g)"), Tipo::Numero, 85, [=](int i) { return ep_fmt(R(i).saldo_g, 1); }, [=](int i, const wxString &v) { R(i).saldo_g = ep_num(v); }});
    c.push_back({ep_u8("Alerta abaixo de (g)"), Tipo::Numero, 110, [=](int i) { return ep_fmt(R(i).minimo_g, 0); }, [=](int i, const wxString &v) { R(i).minimo_g = ep_num(v); }});
    c.push_back({ep_u8("R$/g"), Tipo::Calculado, 70, [=](int i) { return ep_fmt(R(i).custo_por_grama(), 4); }, nullptr});
    c.push_back({ep_u8("Valor em estoque"), Tipo::Calculado, 105, [=](int i) { return ep_reais(std::max(0.0, R(i).saldo_g) * R(i).custo_por_grama()); }, nullptr});

    auto *t = new TabelaEstoque(nb,
        ep_u8("Cadastre cada rolo que você compra. O custo por grama é calculado sozinho. O saldo diminui quando você confirma uma produção; "
           "edite o saldo para ajustes (ex.: pesou o rolo). Linhas em vermelho estão com saldo abaixo do alerta."),
        std::move(c), [this]() { return int(m_dados.rolos.size()); },
        [this]() {
            Rolo r;
            r.id = m_dados.novo_id("r");
            r.nome = "Novo rolo";
            r.saldo_g = 1000.0;
            m_dados.rolos.push_back(r);
        },
        [this](int i) { m_dados.rolos.erase(m_dados.rolos.begin() + i); },
        [this]() { dados_alterados(); });
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
    c.push_back({ep_u8("Nome do insumo"), Tipo::Texto, 320, [=](int i) { return ep_u8(I(i).nome); }, [=](int i, const wxString &v) { I(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Categoria"), Tipo::Texto, 100, [=](int i) { return ep_u8(I(i).categoria); }, [=](int i, const wxString &v) { I(i).categoria = ep_std8(v); }});
    c.push_back({ep_u8("Preço pago (R$)"), Tipo::Numero, 95, [=](int i) { return ep_fmt(I(i).preco_pago); }, [=](int i, const wxString &v) { I(i).preco_pago = ep_num(v); }});
    c.push_back({ep_u8("Quantidade que vem"), Tipo::Numero, 110, [=](int i) { return ep_fmt(I(i).quantidade, 2); }, [=](int i, const wxString &v) { I(i).quantidade = ep_num(v, 1.0); }});
    c.push_back({ep_u8("Unidade"), Tipo::Texto, 60, [=](int i) { return ep_u8(I(i).unidade); }, [=](int i, const wxString &v) { I(i).unidade = ep_std8(v); }});
    c.push_back({ep_u8("Saldo"), Tipo::Numero, 80, [=](int i) { return ep_fmt(I(i).saldo, 2); }, [=](int i, const wxString &v) { I(i).saldo = ep_num(v); }});
    c.push_back({ep_u8("Alerta abaixo de"), Tipo::Numero, 100, [=](int i) { return ep_fmt(I(i).minimo, 2); }, [=](int i, const wxString &v) { I(i).minimo = ep_num(v); }});
    c.push_back({ep_u8("Custo por unidade"), Tipo::Calculado, 105, [=](int i) { return ep_reais(I(i).custo_unidade()); }, nullptr});

    auto *t = new TabelaEstoque(nb,
        ep_u8("Tintas, componentes (argolas, ímãs, NFC...) e embalagens. Informe quanto pagou e quanto vem; o custo por unidade é calculado. "
           "O saldo diminui ao confirmar uma produção que use o insumo."),
        std::move(c), [this]() { return int(m_dados.insumos.size()); },
        [this]() {
            Insumo i;
            i.id = m_dados.novo_id("i");
            i.nome = "Novo insumo";
            m_dados.insumos.push_back(i);
        },
        [this](int i) { m_dados.insumos.erase(m_dados.insumos.begin() + i); },
        [this]() { dados_alterados(); });
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
    c.push_back({ep_u8("Nome"), Tipo::Texto, 200, [=](int i) { return ep_u8(P(i).nome); }, [=](int i, const wxString &v) { P(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Perfil no Bambu contém"), Tipo::Texto, 140, [=](int i) { return ep_u8(P(i).perfil_bambu); }, [=](int i, const wxString &v) { P(i).perfil_bambu = ep_std8(v); }});
    c.push_back({ep_u8("Preço de compra (R$)"), Tipo::Numero, 120, [=](int i) { return ep_fmt(P(i).preco_compra); }, [=](int i, const wxString &v) { P(i).preco_compra = ep_num(v); }});
    c.push_back({ep_u8("Valor residual (%)"), Tipo::Percentual, 105, [=](int i) { return ep_fmt(P(i).valor_residual * 100.0, 1); }, [=](int i, const wxString &v) { P(i).valor_residual = ep_num(v) / 100.0; }});
    c.push_back({ep_u8("Vida útil (h)"), Tipo::Numero, 85, [=](int i) { return ep_fmt(P(i).vida_util_h, 0); }, [=](int i, const wxString &v) { P(i).vida_util_h = ep_num(v); }});
    c.push_back({ep_u8("Potência média (W)"), Tipo::Numero, 105, [=](int i) { return ep_fmt(P(i).potencia_w, 0); }, [=](int i, const wxString &v) { P(i).potencia_w = ep_num(v); }});
    c.push_back({ep_u8("Manutenção (R$/h)"), Tipo::Numero, 105, [=](int i) { return ep_fmt(P(i).manutencao_por_h, 2); }, [=](int i, const wxString &v) { P(i).manutencao_por_h = ep_num(v); }});
    c.push_back({ep_u8("Depreciação (R$/h)"), Tipo::Calculado, 105, [=](int i) { return ep_fmt(P(i).depreciacao_por_h(), 4); }, nullptr});
    c.push_back({ep_u8("Energia (R$/h)"), Tipo::Calculado, 90, [=](int i) { return ep_fmt(P(i).energia_por_h(m_dados.config.tarifa_kwh), 4); }, nullptr});
    c.push_back({ep_u8("Custo máquina (R$/h)"), Tipo::Calculado, 120, [=](int i) { return ep_fmt(P(i).custo_maquina_por_h(m_dados.config.tarifa_kwh), 4); }, nullptr});

    return new TabelaEstoque(nb,
        ep_u8("Em \"Perfil no Bambu contém\", escreva um trecho do nome da impressora no Bambu Studio (ex.: A1, A1 mini, P1S, X1). "
           "Ao ler a placa fatiada, a impressora certa é escolhida sozinha."),
        std::move(c), [this]() { return int(m_dados.impressoras.size()); },
        [this]() {
            Impressora i;
            i.nome = "Nova impressora";
            m_dados.impressoras.push_back(i);
        },
        [this](int i) { m_dados.impressoras.erase(m_dados.impressoras.begin() + i); },
        [this]() { dados_alterados(); });
}

TabelaEstoque *EstoquePrecosPanel::criar_tabela_canais(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    auto C = [this](int i) -> Canal & { return m_dados.canais[i]; };
    c.push_back({ep_u8("Canal"), Tipo::Texto, 320, [=](int i) { return ep_u8(C(i).nome); }, [=](int i, const wxString &v) { C(i).nome = ep_std8(v); }});
    c.push_back({ep_u8("Comissão (% do preço)"), Tipo::Percentual, 130, [=](int i) { return ep_fmt(C(i).comissao * 100.0, 1); }, [=](int i, const wxString &v) { C(i).comissao = ep_num(v) / 100.0; }});
    c.push_back({ep_u8("Taxa fixa por item (R$)"), Tipo::Numero, 130, [=](int i) { return ep_fmt(C(i).taxa_fixa); }, [=](int i, const wxString &v) { C(i).taxa_fixa = ep_num(v); }});
    c.push_back({ep_u8("Taxa de pagamento (%)"), Tipo::Percentual, 130, [=](int i) { return ep_fmt(C(i).taxa_pagamento * 100.0, 1); }, [=](int i, const wxString &v) { C(i).taxa_pagamento = ep_num(v) / 100.0; }});

    return new TabelaEstoque(nb,
        ep_u8("Comissões e taxas mudam com frequência e variam por categoria e faixa de preço. Os valores iniciais são estimativas da planilha — "
           "confira sempre na central do vendedor."),
        std::move(c), [this]() { return int(m_dados.canais.size()); },
        [this]() {
            Canal k;
            k.nome = "Novo canal";
            m_dados.canais.push_back(k);
        },
        [this](int i) { m_dados.canais.erase(m_dados.canais.begin() + i); },
        [this]() { dados_alterados(); });
}

TabelaEstoque *EstoquePrecosPanel::criar_tabela_historico(wxNotebook *nb)
{
    std::vector<TabelaEstoque::Coluna> c;
    // Mais recente primeiro.
    auto H = [this](int i) -> const Movimento & { return m_dados.historico[m_dados.historico.size() - 1 - size_t(i)]; };
    c.push_back({ep_u8("Data"), Tipo::Texto, 120, [=](int i) { return ep_u8(H(i).data); }, nullptr});
    c.push_back({ep_u8("Tipo"), Tipo::Texto, 80, [=](int i) { return ep_u8(H(i).tipo); }, nullptr});
    c.push_back({ep_u8("Produto / motivo"), Tipo::Texto, 260, [=](int i) { return ep_u8(H(i).descricao); }, nullptr});
    c.push_back({ep_u8("Item"), Tipo::Texto, 260, [=](int i) { return ep_u8(H(i).item); }, nullptr});
    c.push_back({ep_u8("Quantidade"), Tipo::Numero, 90, [=](int i) { return ep_fmt(H(i).quantidade, 2); }, nullptr});
    c.push_back({ep_u8("Unidade"), Tipo::Texto, 60, [=](int i) { return ep_u8(H(i).unidade); }, nullptr});
    c.push_back({ep_u8("Preço unit. (R$)"), Tipo::Numero, 100, [=](int i) { return ep_fmt(H(i).valor); }, nullptr});
    return new TabelaEstoque(nb, ep_u8("Cada produção confirmada registra aqui o que saiu do estoque."), std::move(c),
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
