#pragma once
#include <cstdint>
#include <string>
typedef struct packet {
	uint16_t type; //data or cmd
	uint16_t seqn;
	uint32_t total_size; //amount of fragments
	uint16_t lenght;
	std::string payload; //package data
} packet;
