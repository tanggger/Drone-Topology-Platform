#!/usr/bin/env python3
"""
Time Alignment Visualization Tool
Generates visualization charts for time normalization and interpolation resampling.
"""

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from datetime import datetime, timedelta
import argparse
import os

# Set plot style
plt.rcParams['figure.dpi'] = 150
plt.style.use('seaborn-v0_8-whitegrid')

class TimeAlignmentVisualizer:
    def __init__(self, rtk_file=None, processed_file=None):
        """
        Initializes the Time Alignment Visualizer.
        
        Args:
            rtk_file: Path to the original RTK data file.
            processed_file: Path to the preprocessed data file.
        """
        self.rtk_file = rtk_file
        self.processed_file = processed_file
        self.rtk_data = None
        self.processed_data = None
        
    def load_data(self):
        """Loads data."""
        if self.rtk_file and os.path.exists(self.rtk_file):
            print(f"Loading original RTK data: {self.rtk_file}")
            self.rtk_data = pd.read_csv(self.rtk_file)
            
        if self.processed_file and os.path.exists(self.processed_file):
            print(f"Loading preprocessed data: {self.processed_file}")
            self.processed_data = pd.read_csv(self.processed_file)
    
    def create_time_normalization_demo(self):
        """Creates focused demo data for time normalization."""
        print("Creating more focused demo data for time normalization...")
        
        # Simulate original timestamps with irregular intervals for a shorter duration
        start_time = datetime.now()
        original_times = []
        current_time = start_time
        
        # Simulate irregular time intervals
        irregular_intervals = [0.08, 0.12, 0.09, 0.11, 0.13, 0.07, 0.14, 0.10, 0.09, 0.12]
        
        for i in range(12):  # Further reduced number of points to focus the view
            original_times.append(current_time)
            dt = irregular_intervals[i % len(irregular_intervals)]
            current_time += timedelta(seconds=dt)
        
        # Create regular simulation time
        sim_times = np.arange(0, len(original_times) * 0.1, 0.1)
        
        return original_times, sim_times
    
    def plot_time_normalization(self, save_path="time_normalization.png"):
        """Plots a more focused and cleaner time normalization schematic."""
        print("Plotting cleaner time normalization schematic...")
        
        original_times, sim_times = self.create_time_normalization_demo()
        
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 6), sharey=True)
        
        # --- Top plot: Original Timeline (using stem plot for clarity) ---
        markerline, stemlines, baseline = ax1.stem(
            original_times, np.ones(len(original_times)),
            linefmt='r-', markerfmt='ro', basefmt='gray'
        )
        plt.setp(markerline, 'markersize', 6)
        plt.setp(stemlines, 'linewidth', 1.5, 'alpha', 0.7)
        ax1.set_ylabel('Original Timeline', fontsize=11, fontweight='bold')
        ax1.set_title('Time Normalization: Irregular vs. Regular Timestamps', fontsize=13, fontweight='bold')
        ax1.grid(True, which='minor', linestyle=':', linewidth='0.5', color='grey')
        ax1.grid(True, which='major', linestyle='-', linewidth='0.5', color='grey', alpha=0.6)

        # Annotate first few intervals
        intervals = [(original_times[i] - original_times[i-1]).total_seconds() for i in range(1, len(original_times))]
        for i in range(min(5, len(intervals))):
            mid_time = original_times[i] + (original_times[i+1] - original_times[i]) / 2
            ax1.annotate(f'{intervals[i]:.2f}s', 
                        xy=(mid_time, 1.05), xytext=(mid_time, 1.25),
                        ha='center', va='bottom', fontsize=8, color='red',
                        arrowprops=dict(arrowstyle='->', color='red', lw=1, alpha=0.7))

        ax1.xaxis.set_major_formatter(mdates.DateFormatter('%S.%f'))
        ax1.xaxis.set_major_locator(mdates.SecondLocator(interval=1))
        ax1.xaxis.set_minor_locator(mdates.MicrosecondLocator(interval=100000))
        ax1.set_xlabel('Original Time (seconds.microseconds)', fontsize=10)
        ax1.tick_params(axis='x', rotation=30)
        
        # --- Bottom plot: Normalized Simulation Timeline ---
        markerline, stemlines, baseline = ax2.stem(
            sim_times, np.ones(len(sim_times)),
            linefmt='b-', markerfmt='bs', basefmt='gray'
        )
        plt.setp(markerline, 'markersize', 5)
        plt.setp(stemlines, 'linewidth', 1.5, 'alpha', 0.7)
        ax2.set_ylabel('Simulation Timeline', fontsize=11, fontweight='bold')
        
        # Annotate first few regular intervals
        for i in range(min(5, len(sim_times)-1)):
            mid_time = (sim_times[i] + sim_times[i+1]) / 2
            ax2.annotate('0.10s', 
                        xy=(mid_time, 1.05), xytext=(mid_time, 1.25),
                        ha='center', va='bottom', fontsize=8, color='blue',
                        arrowprops=dict(arrowstyle='->', color='blue', lw=1, alpha=0.7))
        
        ax2.set_xlabel('Normalized Simulation Time (s)', fontsize=10)
        ax2.grid(True, which='major', linestyle='-', linewidth='0.5', color='grey', alpha=0.6)
        
        # Set shared Y-axis limits
        ax1.set_ylim(0, 1.6)
        
        # Adjust layout
        plt.tight_layout(rect=[0, 0, 1, 0.96]) # Adjust for suptitle

        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        print(f"Cleaner time normalization plot saved: {save_path}")
        
        return fig
    
    def plot_interpolation_effect(self, save_path="interpolation_effect.png"):
        """Plots the trajectory interpolation resampling effect."""
        print("Plotting trajectory interpolation resampling effect...")
        
        # Create demo data: simulate a drone's trajectory sampling
        np.random.seed(42)
        
        # Original data: irregular sampling
        original_times = np.array([0, 0.08, 0.21, 0.29, 0.43, 0.51, 0.67, 0.74, 0.89, 0.97, 
                                  1.12, 1.18, 1.34, 1.41, 1.58, 1.63, 1.81, 1.87, 1.99])
        original_x = np.cumsum(np.random.normal(2, 0.5, len(original_times)))
        original_y = np.cumsum(np.random.normal(1, 0.3, len(original_times)))
        
        # Interpolated data: regular sampling
        interp_times = np.arange(0, 2.0, 0.1)
        interp_x = np.interp(interp_times, original_times, original_x)
        interp_y = np.interp(interp_times, original_times, original_y)
        
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(14, 10))
        
        # Top-left: Original data point time distribution
        ax1.scatter(original_times, [1]*len(original_times), 
                   alpha=0.8, s=80, c='red', marker='o', label=f'Original Data Points ({len(original_times)})')
        ax1.set_ylim(0.5, 1.5)
        ax1.set_xlabel('Time (s)', fontsize=11)
        ax1.set_ylabel('Data Point', fontsize=11)
        ax1.set_title('Original Data: Irregular Time Sampling', fontsize=12, fontweight='bold', color='red')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # Top-right: Interpolated data point time distribution
        ax2.scatter(interp_times, [1]*len(interp_times), 
                   alpha=0.8, s=80, c='blue', marker='s', label=f'Interpolated Data Points ({len(interp_times)})')
        ax2.set_ylim(0.5, 1.5)
        ax2.set_xlabel('Time (s)', fontsize=11)
        ax2.set_ylabel('Data Point', fontsize=11)
        ax2.set_title('After Interpolation: Regular Time Sampling (Δt=0.1s)', fontsize=12, fontweight='bold', color='blue')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        
        # Bottom-left: Original trajectory
        ax3.plot(original_x, original_y, 'ro-', alpha=0.7, markersize=8, 
                linewidth=2, label='Original Trajectory')
        ax3.scatter(original_x, original_y, c='red', s=100, alpha=0.8, zorder=5)
        ax3.set_xlabel('X coordinate (m)', fontsize=11)
        ax3.set_ylabel('Y coordinate (m)', fontsize=11)
        ax3.set_title('Original Trajectory Path', fontsize=12, fontweight='bold')
        ax3.legend()
        ax3.grid(True, alpha=0.3)
        ax3.axis('equal')
        
        # Bottom-right: Interpolated trajectory comparison
        ax4.plot(original_x, original_y, 'ro-', alpha=0.5, markersize=6, 
                linewidth=1.5, label='Original Trajectory')
        ax4.plot(interp_x, interp_y, 'bs-', alpha=0.8, markersize=4, 
                linewidth=2, label='Interpolated Trajectory')
        ax4.scatter(original_x, original_y, c='red', s=80, alpha=0.6, zorder=4)
        ax4.scatter(interp_x, interp_y, c='blue', s=30, alpha=0.8, zorder=5)
        ax4.set_xlabel('X coordinate (m)', fontsize=11)
        ax4.set_ylabel('Y coordinate (m)', fontsize=11)
        ax4.set_title('Interpolation Effect Comparison', fontsize=12, fontweight='bold')
        ax4.legend()
        ax4.grid(True, alpha=0.3)
        ax4.axis('equal')
        
        # Add statistics
        fig.text(0.02, 0.02, f'Data points increase: {len(original_times)} → {len(interp_times)} (+{len(interp_times)-len(original_times)})', 
                fontsize=10, fontweight='bold')
        fig.text(0.35, 0.02, f'Time precision improvement: Irregular interval → 0.1s regular interval', 
                fontsize=10, fontweight='bold')
        
        plt.tight_layout()
        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        print(f"Interpolation effect plot saved: {save_path}")
        
        return fig
    
    def create_data_density_comparison(self, save_path="data_density_comparison.png"):
        """Creates data density comparison plot."""
        print("Creating data density comparison plot...")
        
        # Simulate data density change
        time_windows = np.arange(0, 10, 0.5)
        
        # Original data: uneven density
        np.random.seed(42)
        original_density = np.random.poisson(8, len(time_windows)) + np.random.randint(2, 12, len(time_windows))
        
        # Interpolated data: uniform density
        target_density = 10
        interpolated_density = [target_density] * len(time_windows)
        
        fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(12, 10))
        
        # Top plot: Original data density
        bars1 = ax1.bar(time_windows, original_density, width=0.4, 
                        alpha=0.7, color='red', label='Original Data Density')
        ax1.set_ylabel('Number of Data Points', fontsize=11)
        ax1.set_title('Original Data: Data Point Density Distribution in Time Windows', fontsize=12, fontweight='bold', color='red')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # Annotate values on bars
        for bar, value in zip(bars1, original_density):
            ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1, 
                    str(value), ha='center', va='bottom', fontsize=8)
        
        # Middle plot: Interpolated data density
        bars2 = ax2.bar(time_windows, interpolated_density, width=0.4, 
                        alpha=0.7, color='blue', label='Interpolated Data Density')
        ax2.set_ylabel('Number of Data Points', fontsize=11)
        ax2.set_title('After Interpolation: Uniform Data Point Density Distribution', fontsize=12, fontweight='bold', color='blue')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        
        # Bottom plot: Density comparison
        width = 0.18
        x_pos = time_windows
        ax3.bar(x_pos - width/2, original_density, width, 
               alpha=0.7, color='red', label='Original Density')
        ax3.bar(x_pos + width/2, interpolated_density, width, 
               alpha=0.7, color='blue', label='Interpolated Density')
        
        ax3.set_xlabel('Time Window (s)', fontsize=11)
        ax3.set_ylabel('Number of Data Points', fontsize=11)
        ax3.set_title('Data Density Comparison', fontsize=12, fontweight='bold')
        ax3.legend()
        ax3.grid(True, alpha=0.3)
        
        # Add statistics
        original_std = np.std(original_density)
        interpolated_std = np.std(interpolated_density)
        
        fig.text(0.02, 0.02, f'Original Data Std Dev: {original_std:.2f}', 
                fontsize=10, color='red', fontweight='bold')
        fig.text(0.25, 0.02, f'Interpolated Data Std Dev: {interpolated_std:.2f}', 
                fontsize=10, color='blue', fontweight='bold')
        fig.text(0.48, 0.02, f'Uniformity Improvement: {((original_std-interpolated_std)/original_std*100):.1f}%', 
                fontsize=10, fontweight='bold')
        
        plt.tight_layout()
        plt.savefig(save_path, dpi=300, bbox_inches='tight')
        print(f"Data density comparison plot saved: {save_path}")
        
        return fig
    
    def generate_all_visualizations(self, output_dir="time_alignment_plots"):
        """Generates all time alignment related visualizations."""
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        
        print(f"Generating time alignment visualization plots in directory: {output_dir}")
        
        # Generate each plot
        fig1 = self.plot_time_normalization(
            os.path.join(output_dir, "time_normalization.png"))
        
        fig2 = self.plot_interpolation_effect(
            os.path.join(output_dir, "interpolation_effect.png"))
        
        fig3 = self.create_data_density_comparison(
            os.path.join(output_dir, "data_density_comparison.png"))
        
        # Show plots if in an interactive environment
        try:
            plt.show()
        except:
            pass
        
        print(f"\nAll visualizations complete! Files saved in: {output_dir}/")
        print("Generated files:")
        print("- time_normalization.png: Time normalization schematic")
        print("- interpolation_effect.png: Trajectory interpolation resampling effect") 
        print("- data_density_comparison.png: Data density comparison")

def main():
    parser = argparse.ArgumentParser(description='Time Alignment Visualization Tool')
    parser.add_argument('--rtk_file', type=str, help='Input raw RTK data file')
    parser.add_argument('--processed_file', type=str, help='Input preprocessed data file')
    parser.add_argument('--output_dir', type=str, default='time_alignment_plots', 
                       help='Output directory')
    
    args = parser.parse_args()
    
    # Create visualizer
    visualizer = TimeAlignmentVisualizer(args.rtk_file, args.processed_file)
    
    # Load data if files are provided
    if args.rtk_file or args.processed_file:
        visualizer.load_data()
    
    # Generate all visualizations
    visualizer.generate_all_visualizations(args.output_dir)

if __name__ == "__main__":
    main()