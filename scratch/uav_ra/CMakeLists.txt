build_exec(
  EXECNAME uav_resource_allocation
  SOURCE_FILES main.cc
               non_cooperative_attack.cc
               non_cooperative_inference.cc
               output_runtime.cc
               scenario_environment.cc
               simulation_setup.cc
               topology_control.cc
               traffic_metrics.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/uav_ra
)
