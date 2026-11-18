// Teste do estoque: baixa de producao, avisos e ida/volta em JSON.
// Compilar: g++ -std=c++17 -DESTOQUE_SEM_BOOST -I src tests/estoque_precos/test_store.cpp
//           src/slic3r/GUI/EstoquePrecos/EstoquePrecosStore.cpp src/slic3r/GUI/EstoquePrecos/EstoquePrecosCore.cpp
#include "slic3r/GUI/EstoquePrecos/EstoquePrecosStore.hpp"

#include <cmath>
#include <cstdio>

using namespace Slic3r::EstoquePrecos;

static int falhas = 0;
#define CONFERE(cond) do { if (!(cond)) { std::printf("FALHOU linha %d: %s\n", __LINE__, #cond); ++falhas; } } while (0)

int main()
{
    Dados d = Dados::padrao();
    CONFERE(d.canais.size() == 10);
    CONFERE(d.impressora_para_perfil("Bambu Lab A1 0.4 nozzle") == 0);
    CONFERE(d.impressora_para_perfil("Bambu Lab X1 Carbon 0.4 nozzle") == -1);

    Rolo r;
    r.id = d.novo_id("r"); r.nome = "PLA Branco"; r.preco_pago = 96.05; r.peso_g = 1000; r.saldo_g = 100; r.minimo_g = 50;
    d.rolos.push_back(r);
    Insumo i;
    i.id = d.novo_id("i"); i.nome = "Argola"; i.preco_pago = 39.9; i.quantidade = 100; i.saldo = 10;
    d.insumos.push_back(i);
    CONFERE(r.id == "r1" && i.id == "i1");
    CONFERE(std::fabs(d.rolos[0].custo_por_grama() - 0.09605) < 1e-12);
    CONFERE(std::fabs(d.insumos[0].custo_unidade() - 0.399) < 1e-12);

    // 2 placas de 30 g, 3 unidades por placa, 1 argola por unidade.
    std::vector<std::string> avisos = d.confirmar_producao("Chaveiro", 2, 3, {{"r1", 30.0}}, {{"i1", 1.0}}, 12.9, "2026-11-17 20:00");
    CONFERE(std::fabs(d.rolos[0].saldo_g - 40.0) < 1e-9);
    CONFERE(std::fabs(d.insumos[0].saldo - 4.0) < 1e-9);
    CONFERE(d.historico.size() == 2);
    CONFERE(avisos.size() == 1); // estoque baixo no rolo (40 < 50)

    avisos = d.confirmar_producao("Chaveiro", 1, 3, {{"r1", 50.0}}, {}, 12.9, "2026-11-17 21:00");
    CONFERE(d.rolos[0].saldo_g < 0.0);
    CONFERE(avisos.size() == 1); // saldo negativo

    Dados e;
    std::string erro;
    CONFERE(e.from_json(d.to_json(), erro));
    CONFERE(e.rolos.size() == 1 && e.insumos.size() == 1 && e.historico.size() == 3);
    CONFERE(std::fabs(e.rolos[0].saldo_g - d.rolos[0].saldo_g) < 1e-9);
    CONFERE(e.canais.size() == 10 && e.impressoras.size() == 1);

    CONFERE(!e.from_json("{ invalido", erro));
    CONFERE(e.rolos.size() == 1); // JSON invalido nao apaga os dados

    std::printf("estoque: %d falhas\n", falhas);
    return falhas == 0 ? 0 : 1;
}
