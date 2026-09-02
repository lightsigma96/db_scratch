#include "proto/client_server_common.pb.h"
#include "src/server_helpers.hpp"
#include "tui/headers/components.hpp"
#include "tui/headers/input_handler.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

constexpr size_t MAX_PAYLOAD_SIZE   = 2048;
constexpr char   unix_server_path[] = "/tmp/db_scratch.sock";

void TUI_Pipeline(Structure &st, bool first_load) {

    client_server_common::Response res;
    client_server_common::Request  request;
    if (!first_load) {

        // const std::string &input = request.input();

        for (std::unique_ptr<TextBox> &textbox : st.textbox) {
            if (textbox->component_id == "query_input") {
                request.set_input(textbox->get_inner_text().data());
                // input = textbox->get_inner_text();
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

        request.set_schema_name(curr_active_schema.data());

        std::string payload;

        if (!request.SerializeToString(&payload)) {
            printf("ERROR : Serialization");
            return;
        }

        if (payload.size() > MAX_PAYLOAD_SIZE) {
            printf("ERROR : Payload too large");
            return;
        }

        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

        if (sock_fd < 0) {
            perror("socket");
            return;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, unix_server_path, sizeof(addr.sun_path) - 1);

        if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("connect");
            close(sock_fd);
            return;
        }

        uint32_t payload_size = htonl(static_cast<uint32_t>(payload.size()));

        if (!server::send_all(sock_fd, &payload_size, sizeof(payload_size)) || !server::send_all(sock_fd, payload.data(), payload.size())) {
            printf("ERROR : Sending request");
            close(sock_fd);
            return;
        }

        uint32_t response_size_net;

        if (!server::recv_all(sock_fd, &response_size_net, sizeof(response_size_net))) {
            printf("ERROR : Receiving response size");
            close(sock_fd);
            return;
        }

        size_t response_size = ntohl(response_size_net);

        if (response_size > MAX_PAYLOAD_SIZE) {
            printf("ERROR : Response too large");
            close(sock_fd);
            return;
        }

        std::string response_payload(response_size, '\0');

        if (!server::recv_all(sock_fd, response_payload.data(), response_size)) {
            printf("ERROR : Receiving response");
            close(sock_fd);
            return;
        }

        close(sock_fd);

        if (!res.ParseFromString(response_payload)) {
            printf("ERROR : Deserialization");
            return;
        }
    }

    int accord_idx = 0;

    for (; accord_idx < st.accordion.size(); accord_idx++) {

        if (st.accordion[accord_idx]->component_id == "schema_display") {

            const auto &res_schemas = res.schemas();

            for (int i = 0; i < res_schemas.size(); i++) {

                const auto &r = res_schemas.Get(i);

                st.accordion[accord_idx]->fill_entry(r.schema_name(), i);

                for (int j = 0; j < r.tables().size(); j++) {

                    const auto &t = r.tables().Get(j);

                    st.accordion[accord_idx]->fill_childs(t.table_name(), i);
                }
            }

            break;
        }
    }

    if (res.results_size() > 0) {

        int table_idx = 0;

        for (int i = 0; i < st.table.size(); i++) {

            if (st.table[i]->component_id == "display_table") {
                table_idx = i;
                break;
            }
        }

        int parent_selected = st.accordion[accord_idx]->which_selected;

        int child_selected = st.accordion[accord_idx]->entry[parent_selected].sub_child_selected;

        const auto &table = res.schemas(parent_selected).tables(child_selected);

        int num_cols = table.columns_size();

        st.table[table_idx]->divisions = num_cols;

        st.table[table_idx]->rows.assign(res.results_size(), std::vector<std::string>(num_cols, " "));

        st.table[table_idx]->headers.assign(num_cols, " ");

        for (int h = 0; h < table.columns_size(); h++) {

            st.table[table_idx]->fill_headers(table.columns(h).column_name(), h);
        }

        for (int r = 0; r < res.results_size(); r++) {

            const auto &row = res.results(r);

            for (int v = 0; v < row.values_size(); v++) {

                const auto &value = row.values(v);

                switch (value.value_case()) {

                    case client_server_common::Value::kIntValue:
                        st.table[table_idx]->fill_rows(std::to_string(value.int_value()), r, v);
                        break;

                    case client_server_common::Value::kStringValue:
                        st.table[table_idx]->fill_rows(value.string_value(), r, v);
                        break;

                    case client_server_common::Value::kFloatValue:
                        st.table[table_idx]->fill_rows(std::to_string(value.float_value()), r, v);
                        break;

                    case client_server_common::Value::VALUE_NOT_SET:
                        break;
                }
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

    TUI_Pipeline(st, true);
    while (1) {

        screen.reset_region(1, 1, screen.max_cols - 1, screen.max_rows - 1, {});

        int bytes_read = read(STDIN_FILENO, &in, 1);
        if (bytes_read <= 0)
            continue;

        if (in == 3)
            return 0;

        input_handler.read(in, events);

        if (events.SUBMIT) {
            TUI_Pipeline(st, false);
        }

        st.draw_structure();
        write(STDOUT_FILENO, "\033[H", 3);
        screen.Render();
        events = {};
    }

    return 0;
}
