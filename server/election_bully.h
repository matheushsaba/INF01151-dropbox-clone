#pragma once
#include <vector>
#include <string>

void bully_init(const std::string& my_ip);       // start listener once
void bully_start();                              // triggered on HB timeout
void bully_set_peer_list(const std::vector<std::string>& peers);