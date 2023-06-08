work_group_sizes="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"
repetition_count=100
repetition_delay=1000


bench() {
	# $1: dataset
	# $2: seed dimension x
	# $3: seed dimension y
	# $4: seed dimension z
	# $5: steps
	# $6: delta time
	for work_group_size_x in $work_group_sizes; do
		for work_group_size_y in $work_group_sizes; do
			for work_group_size_z in $work_group_sizes; do
				if [[ `expr $work_group_size_x "*" $work_group_size_y "*" $work_group_size_z` -le 16 ]]; then
					./build/Release/bc6h-integrator.exe \
						$1 \
						--work_group_size_x=$work_group_size_x \
						--work_group_size_y=$work_group_size_y \
						--work_group_size_z=$work_group_size_z \
						--seed_dimension_x=$2 \
						--seed_dimension_y=$3 \
						--seed_dimension_z=$4 \
						--integration_steps=$5 \
						--batch_size=$5 \
						--delta_time=$6 \
						--repetition_delay=$repetition_delay \
						--repetition_count=$repetition_count \
						--explicit_interpolation

					./build/Release/bc6h-integrator.exe \
						$1 \
						--work_group_size_x=$work_group_size_x \
						--work_group_size_y=$work_group_size_y \
						--work_group_size_z=$work_group_size_z \
						--seed_dimension_x=$2 \
						--seed_dimension_y=$3 \
						--seed_dimension_z=$4 \
						--integration_steps=$5 \
						--batch_size=$5 \
						--delta_time=$6 \
						--repetition_delay=$repetition_delay \
						--repetition_count=$repetition_count
				fi
			done
		done
	done
}

# ABC
bench "/c/datasets/abc_bc6h_highest.ktx" 25 25 15 15100 0.01
bench "/c/datasets/abc_f16.raw" 25 25 15 15100 0.01
bench "/c/datasets/abc_f32.raw" 25 25 15 15100 0.01
bench "/c/datasets/abc_high_resolution_bc6h.raw" 25 25 15 15100 0.01

# Halfcylinder
bench "/c/datasets/halfcylinder_bc6h_highest.ktx" 25 25 12 20100 0.01
bench "/c/datasets/halfcylinder_f16.raw" 25 25 12 20100 0.01
bench "/c/datasets/halfcylinder_f32.raw" 25 25 12 20100 0.01

# Tangaroa
bench "/c/datasets/tangaroa_bc6h_highest.ktx" 20 25 15 15100 0.01
bench "/c/datasets/tangaroa_f16.raw" 20 25 15 15100 0.01
bench "/c/datasets/tangaroa_f32.raw" 20 25 15 15100 0.01
