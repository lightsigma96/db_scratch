#include "client_server_common.hpp"
#include "proto/client_server_common.pb.h"
#include "tui/headers/components.hpp"
#include "tui/headers/input_handler.hpp"
#include <type_traits>

void TUI_Pipeline(Structure &st, client_server_common::Request &request,
                  bool first_load) {

    std::string_view input = request.input();
    // input.text_box_input = input_text;

    for (std::unique_ptr<TextBox> &textbox : st.textbox) {
        if (textbox->component_id == "query_input") {
            input =
                textbox
                    ->get_inner_text(); // even if obj is destroyed we fulfilled
                                        // our purpose with this string
            break;
        }
    }

    std::string curr_active_schema = "";
    for (std::unique_ptr<Accordion> &ac : st.accordion) {
        if (ac->component_id == "schema_display" && ac->entry.size() != 0) {
            curr_active_schema = ac->entry[ac->which_selected].name;
            break;
        }
    }

    // replace here with TCP connection talking

    client_server_common::Response res =
        DB_Pipeline(curr_active_schema, input, first_load);

    int accord_idx = 0;

    if (res.schemas.has_value()) {
        for (accord_idx = 0; accord_idx < st.accordion.size(); accord_idx++) {
            if (st.accordion[accord_idx]->component_id == "schema_display") {
                for (int i = 0; i < res.schemas.value().size(); i++) {
                    st.accordion[accord_idx]->fill_entry(
                        res.schemas.value()[i].schema_name, i);
                    for (int j = 0; j < res.schemas.value()[i].tables.size();
                         j++) {
                        st.accordion[accord_idx]->fill_childs(
                            res.schemas.value()[i].tables[j].table_name, i);
                    }
                }
                break;
            }
        }
    }

    if (res.results.has_value()) {
        int table_idx = 0;
        for (int i = 0; i < st.table.size(); i++) {
            if (st.table[i]->component_id == "display_table") {
                st.table[i]->divisions = res.results.value().size();
                table_idx              = i;
                break;
            }
        }
        int parent_selected = st.accordion[accord_idx]->which_selected;
        int child_selected =
            st.accordion[accord_idx]->entry[parent_selected].sub_child_selected;

        // remember to resize according to res.results (number of col) after
        // setting divisions
        st.table[table_idx]->divisions = res.schemas.value()[parent_selected]
                                             .tables[child_selected]
                                             .columns.size();
        int num_cols                   = res.schemas.value()[parent_selected]
                                             .tables[child_selected]
                                             .columns.size();
        st.table[table_idx]->rows.assign(
            res.results.value().size(),
            std::vector<std::string>(num_cols, " "));
        st.table[table_idx]->headers.assign(num_cols, " ");

        int h = 0;

        for (h = 0; h < res.schemas.value()[parent_selected]
                            .tables[child_selected]
                            .columns.size();
             h++) {
            st.table[table_idx]->fill_headers(
                res.schemas.value()[parent_selected]
                    .tables[child_selected]
                    .columns[h]
                    .column_name,
                h);
        }

        for (int r = 0; r < res.results.value().size(); r++) {
            for (int v = 0; v < res.results.value()[r].row.size(); v++) {
                std::visit(
                    [&](auto &&val) {
                        using T = std::decay_t<decltype(val)>;

                        if constexpr (std::is_same_v<int, T>) {
                            st.table[table_idx]->fill_rows(std::to_string(val),
                                                           r, v);
                        } else if constexpr (std::is_same_v<std::string, T>) {
                            st.table[table_idx]->fill_rows(val, r, v);
                        } else if constexpr (std::is_same_v<float, T>) {
                            st.table[table_idx]->fill_rows(std::to_string(val),
                                                           r, v);
                        }
                    },
                    (res.results.value()[r].row[v]));
            }
        }
    }
}

int main() {
    Renderer::Screen screen;

    // --- TextBox (top-left)
    TextBox textbox1(screen,
                     37,  // row
                     2,   // col
                     3,   // height
                     140, // width
                     MAGENTA, "query_input");

    // --- Table (below textbox)
    Table table1(screen,
                 2,      // col
                 2,      // row
                 140,    // width
                 38 - 3, // height
                 1, 3, RED, "display_table");

    // --- Accordion (right side)
    Accordion             acc1(screen,
                               2 + 140 + 2, // col
                               2,           // row
                               43,          // width
                               38,          // height
                               3, RED, "schema_display");
    InputHandlers::Events events = {};

    Structure st;
    st.table.push_back(std::make_unique<Table>(table1));
    st.components.push_back(st.table.back().get());

    st.textbox.push_back(std::make_unique<TextBox>(textbox1));
    st.components.push_back(st.textbox.back().get());

    st.accordion.push_back(std::make_unique<Accordion>(acc1));
    st.components.push_back(st.accordion.back().get());

    InputHandlers::Stdin_Handler input_handler(st);

    char in;
    write(STDOUT_FILENO, "\033[?25l", 6);
    write(STDOUT_FILENO, "\033[2J\033[H", 7);

    client_server_common::OnWire onwire_proto;

    TUI_Pipeline(st, onwire_proto, true);
    while (1) {

        screen.reset_region(1, 1, screen.max_cols - 1, screen.max_rows - 1, {});

        int bytes_read = read(STDIN_FILENO, &in, 1);
        if (bytes_read <= 0)
            continue;

        if (in == 3)
            return 0;

        input_handler.read(in, events);

        if (events.SUBMIT) {
            TUI_Pipeline(st, onwire_proto, false);
        }

        st.draw_structure();
        write(STDOUT_FILENO, "\033[H", 3);
        screen.Render();
        events = {};
    }

    return 0;
}
