# PC test build of the BStone game code on the BlakeStone32X platform layer.
# Run:  BS32X_DATA=/path/to/BLAKE BS32X_SCRIPT=../tools/test/newgame.script \
#       BS32X_FRAMES=3000 BS32X_SHOTS=all BS32X_OUT=out ./bstone_host
GAME = 3d_act1 3d_act2 3d_agent 3d_debug 3d_draw 3d_draw2 3d_game 3d_inter 3d_main \
       3d_menu 3d_msgs 3d_play 3d_scale 3d_state colormap d3_d2 d3_dr2 id_ca id_in id_pm \
       id_sd id_sdd id_us id_us_1 id_vh jm_cio jm_free jm_io jm_lzh jm_tp markhack movie \
       scale stub vgapal bstone_binary_reader bstone_binary_writer bstone_cl_args \
       bstone_crc32 bstone_istream bstone_memory_binary_reader bstone_memory_stream \
       bstone_string_helper
PORT = vl_port file_stream_port host_platform bs32x_render bs32x_levels
OBJ = $(addprefix obj/,$(addsuffix .o,$(GAME) $(PORT))) obj/lz4d.o
CXXFLAGS = -MMD -MP -std=c++14 -O1 -g -fno-exceptions -DBS32X -I../port -I../bstone -include bs32x.h -w

bstone_host: $(OBJ)
	$(CXX) -o $@ $(OBJ)

obj/%.o: ../bstone/%.cpp ../port/bs32x.h | obj
	$(CXX) $(CXXFLAGS) -c $< -o $@
obj/%.o: ../port/%.cpp ../port/bs32x.h | obj
	$(CXX) $(CXXFLAGS) -c $< -o $@
obj/lz4d.o: ../port/lz4d.c | obj
	$(CC) -O1 -g -c $< -o $@
obj:
	mkdir -p obj
clean:
	rm -rf obj bstone_host

-include $(OBJ:.o=.d)
