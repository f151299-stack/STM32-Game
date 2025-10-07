cmake_minimum_required(VERSION 3.15)

project(f401_gen_sin LANGUAGES C CXX ASM)

find_bsp(ID com.sysprogs.project_importers.stm32.cubemx SOURCE_PROJECT f401_gen_sin.ioc)

add_bsp_based_executable(NAME f401_gen_sin
	GENERATE_BIN
	GENERATE_MAP)

