#include <event.h>

#include <core/md_compiler.h>
#include <core/md_log.h>
#include <core/md_array.h>
#include <core/md_vec_math.h>
#include <core/md_bitfield.h>
#include <core/md_arena_allocator.h>
#include <core/md_os.h>

#include <md_script.h>
#include <md_util.h>

#include <viamd.h>
#include <serialization_utils.h>
#include <imgui_widgets.h>
#include <implot_widgets.h>
#include <implot_internal.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef MEGABYTES
#define MEGABYTES(x) ((x) * 1024 * 1024)
#endif

// Property selection buttons instead of drag-drop

struct ScatterSeries {
    md_array(float) x_data = 0;
    md_array(float) y_data = 0;
    md_array(int) frame_indices = 0;
    char name[64] = "";
    ImVec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct Correlation : viamd::EventHandler {
    char error[256] = "";
    bool show_window = false;
    
    // Property selection using main application's display properties
    int x_property_idx = -1;
    int y_property_idx = -1;
    
    // Scatter plot data
    md_array(ScatterSeries) series = 0;
    
    // Interaction
    int hovered_point = -1;
    int clicked_frame = -1;
    
    md_allocator_i* arena = 0;
    ApplicationState* app_state = 0;

    Correlation() { viamd::event_system_register_handler(*this); }

    void process_events(const viamd::Event* events, size_t num_events) final {
        for (size_t i = 0; i < num_events; ++i) {
            const viamd::Event& e = events[i];

            switch (e.type) {
            case viamd::EventType_ViamdInitialize: {
                app_state = (ApplicationState*)e.payload;
                arena = md_arena_allocator_create(app_state->allocator.persistent, MEGABYTES(1));
                break;
            }
            case viamd::EventType_ViamdShutdown:
                md_arena_allocator_destroy(arena);
                break;
            case viamd::EventType_ViamdFrameTick:
                draw_window();
                break;
            case viamd::EventType_ViamdWindowDrawMenu:
                ImGui::Checkbox("Correlation", &show_window);
                break;
            case viamd::EventType_ViamdDeserialize: {
                viamd::deserialization_state_t& state = *(viamd::deserialization_state_t*)e.payload;
                if (str_eq(viamd::section_header(state), STR_LIT("Correlation"))) {
                    str_t ident, arg;
                    while (viamd::next_entry(ident, arg, state)) {
                        if (str_eq(ident, STR_LIT("show_window"))) {
                            viamd::extract_bool(show_window, arg);
                        } else if (str_eq(ident, STR_LIT("x_property_idx"))) {
                            viamd::extract_int(x_property_idx, arg);
                        } else if (str_eq(ident, STR_LIT("y_property_idx"))) {
                            viamd::extract_int(y_property_idx, arg);
                        }
                    }
                }
                break;
            }
            case viamd::EventType_ViamdSerialize: {
                viamd::serialization_state_t& state = *(viamd::serialization_state_t*)e.payload;
                viamd::write_section_header(state, STR_LIT("Correlation"));
                viamd::write_bool(state, STR_LIT("show_window"), show_window);
                viamd::write_int(state, STR_LIT("x_property_idx"), x_property_idx);
                viamd::write_int(state, STR_LIT("y_property_idx"), y_property_idx);
                break;
            }
            default:
                break;
            }
        }
    }

    void update_scatter_data() {
        if (!app_state || x_property_idx < 0 || y_property_idx < 0) {
            return;
        }
        
        const int num_props = (int)md_array_size(app_state->display_properties);
        if (x_property_idx >= num_props || y_property_idx >= num_props) {
            return;
        }
        
        // Check that properties exist and have valid data
        if (x_property_idx >= num_props || y_property_idx >= num_props) {
            return;
        }
        
        // Only process temporal properties with valid data
        // Use helper functions to safely access DisplayProperty without complete type
        if (get_display_property_type_by_index(app_state, x_property_idx) != DisplayPropertyType_Temporal || 
            get_display_property_type_by_index(app_state, y_property_idx) != DisplayPropertyType_Temporal ||
            !has_display_property_data_by_index(app_state, x_property_idx) || 
            !has_display_property_data_by_index(app_state, y_property_idx)) {
            return;
        }
        
        // Clear existing series
        for (size_t i = 0; i < md_array_size(series); ++i) {
            md_array_free(series[i].x_data, arena);
            md_array_free(series[i].y_data, arena);
            md_array_free(series[i].frame_indices, arena);
        }
        md_array_resize(series, 0, arena);
        
        // Get property data using helper function
        const md_script_property_data_t* x_data = get_display_property_data_by_index(app_state, x_property_idx);
        const md_script_property_data_t* y_data = get_display_property_data_by_index(app_state, y_property_idx);
        
        const size_t num_frames = x_data->dim[0];
        const size_t x_values_per_frame = x_data->num_values / num_frames;
        const size_t y_values_per_frame = y_data->num_values / num_frames;
        
        if (num_frames != y_data->dim[0]) {
            strcpy(error, "Properties have different temporal dimensions");
            return;
        }
        
        // Clear any previous error
        error[0] = '\0';
        
        if (x_values_per_frame > 1 || y_values_per_frame > 1) {
            // Handle array properties - create multiple series
            const size_t max_series = ImMax(x_values_per_frame, y_values_per_frame);
            
            for (size_t s = 0; s < max_series; ++s) {
                ScatterSeries scatter = {};
                snprintf(scatter.name, sizeof(scatter.name), "%s[%zu] vs %s[%zu]", 
                    get_display_property_label_by_index(app_state, x_property_idx), x_values_per_frame > 1 ? s : 0,
                    get_display_property_label_by_index(app_state, y_property_idx), y_values_per_frame > 1 ? s : 0);
                
                // Assign a color based on series index
                scatter.color = ImPlot::GetColormapColor((int)s);
                
                for (size_t f = 0; f < num_frames; ++f) {
                    float x_val, y_val;
                    
                    if (x_values_per_frame > 1) {
                        if (s < x_values_per_frame) {
                            x_val = x_data->values[f * x_values_per_frame + s];
                        } else {
                            continue; // Skip this series if x doesn't have enough values
                        }
                    } else {
                        x_val = x_data->values[f];
                    }
                    
                    if (y_values_per_frame > 1) {
                        if (s < y_values_per_frame) {
                            y_val = y_data->values[f * y_values_per_frame + s];
                        } else {
                            continue; // Skip this series if y doesn't have enough values
                        }
                    } else {
                        y_val = y_data->values[f];
                    }
                    
                    md_array_push(scatter.x_data, x_val, arena);
                    md_array_push(scatter.y_data, y_val, arena);
                    md_array_push(scatter.frame_indices, (int)f, arena);
                }
                
                if (md_array_size(scatter.x_data) > 0) {
                    md_array_push(series, scatter, arena);
                }
            }
        } else {
            // Simple case - single series
            ScatterSeries scatter = {};
            snprintf(scatter.name, sizeof(scatter.name), "%s vs %s", 
                get_display_property_label_by_index(app_state, x_property_idx), get_display_property_label_by_index(app_state, y_property_idx));
            scatter.color = ImPlot::GetColormapColor(0);
            
            for (size_t f = 0; f < num_frames; ++f) {
                md_array_push(scatter.x_data, x_data->values[f], arena);
                md_array_push(scatter.y_data, y_data->values[f], arena);
                md_array_push(scatter.frame_indices, (int)f, arena);
            }
            
            md_array_push(series, scatter, arena);
        }
    }

    void draw_window() {
        if (!show_window) return;
        
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Correlation Plot", &show_window, ImGuiWindowFlags_NoFocusOnAppearing)) {
            if (!app_state) {
                ImGui::Text("Application not initialized");
                ImGui::End();
                return;
            }
            
            const int num_props = (int)md_array_size(app_state->display_properties);
            
            // Get available space for plot area
            ImVec2 plot_avail = ImGui::GetContentRegionAvail();
            float y_button_width = 150.0f;
            float x_button_height = 40.0f;
            float clear_button_width = 80.0f;
            
            // Y-axis property selection button on the left
            ImGui::BeginChild("YAxisButton", ImVec2(y_button_width, plot_avail.y - x_button_height), false);
            
            ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 20.0f);
            ImGui::Text("Y-Axis Property:");
            
            // Y-axis property selection button
            const char* y_button_label = "Select Property";
            char y_display_text[128] = "Select Property";
            
            if (y_property_idx >= 0) {
                const char* y_label = get_display_property_label_by_index(app_state, y_property_idx);
                const char* y_unit = get_display_property_unit_by_index(app_state, y_property_idx, 1);
                if (strlen(y_unit) > 0) {
                    snprintf(y_display_text, sizeof(y_display_text), "%s (%s)", y_label, y_unit);
                } else {
                    snprintf(y_display_text, sizeof(y_display_text), "%s", y_label);
                }
            }
            
            if (ImGui::Button(y_display_text, ImVec2(-1, 0))) {
                ImGui::OpenPopup("Y_Property_Selector");
            }
            
            if (ImGui::BeginPopup("Y_Property_Selector")) {
                ImGui::Text("Select Y-Axis Property:");
                ImGui::Separator();
                
                bool has_temporal_props = false;
                for (int i = 0; i < num_props; ++i) {
                    if (get_display_property_type_by_index(app_state, i) == DisplayPropertyType_Temporal) {
                        has_temporal_props = true;
                        const char* label = get_display_property_label_by_index(app_state, i);
                        ImVec4 color = get_display_property_color_by_index(app_state, i);
                        
                        ImPlot::ItemIcon(color);
                        ImGui::SameLine();
                        
                        if (ImGui::Selectable(label, y_property_idx == i)) {
                            y_property_idx = i;
                            update_scatter_data();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                
                if (!has_temporal_props) {
                    ImGui::Text("No temporal properties available.");
                    ImGui::Text("Define and evaluate properties in the script editor.");
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Plot area in the center
            ImGui::BeginChild("PlotArea", ImVec2(plot_avail.x - y_button_width - clear_button_width, plot_avail.y - x_button_height), false);
            
            // Always display the scatter plot if we have data
            if (x_property_idx >= 0 && y_property_idx >= 0 && md_array_size(series) > 0) {
                if (ImPlot::BeginPlot("Property Correlation", ImVec2(-1, -1))) {
                    
                    // Set axis labels with units
                    if (x_property_idx >= 0 && x_property_idx < num_props) {
                        const char* x_unit = get_display_property_unit_by_index(app_state, x_property_idx, 0);
                        char x_axis_label[128];
                        if (strlen(x_unit) > 0) {
                            snprintf(x_axis_label, sizeof(x_axis_label), "%s (%s)", 
                                get_display_property_label_by_index(app_state, x_property_idx), x_unit);
                        } else {
                            snprintf(x_axis_label, sizeof(x_axis_label), "%s", 
                                get_display_property_label_by_index(app_state, x_property_idx));
                        }
                        ImPlot::SetupAxis(ImAxis_X1, x_axis_label);
                    }
                    if (y_property_idx >= 0 && y_property_idx < num_props) {
                        const char* y_unit = get_display_property_unit_by_index(app_state, y_property_idx, 1);
                        char y_axis_label[128];
                        if (strlen(y_unit) > 0) {
                            snprintf(y_axis_label, sizeof(y_axis_label), "%s (%s)", 
                                get_display_property_label_by_index(app_state, y_property_idx), y_unit);
                        } else {
                            snprintf(y_axis_label, sizeof(y_axis_label), "%s", 
                                get_display_property_label_by_index(app_state, y_property_idx));
                        }
                        ImPlot::SetupAxis(ImAxis_Y1, y_axis_label);
                    }
                    
                    for (size_t s = 0; s < md_array_size(series); ++s) {
                        const ScatterSeries& scatter = series[s];
                        if (md_array_size(scatter.x_data) > 0) {
                            ImPlot::PushStyleColor(ImPlotCol_MarkerFill, scatter.color);
                            ImPlot::PlotScatter(scatter.name, 
                                scatter.x_data, scatter.y_data, 
                                (int)md_array_size(scatter.x_data));
                            ImPlot::PopStyleColor();
                            
                            // Check for hover/click on points
                            if (ImPlot::IsPlotHovered()) {
                                // Find closest point in screen space
                                float min_dist_sq = FLT_MAX;
                                int closest_point = -1;
                                
                                for (size_t p = 0; p < md_array_size(scatter.x_data); ++p) {
                                    // Convert plot coordinates to screen space for distance calculation
                                    ImVec2 screen_pos = ImPlot::PlotToPixels(scatter.x_data[p], scatter.y_data[p]);
                                    ImPlotPoint mouse_plot = ImPlot::GetPlotMousePos();
                                    ImVec2 mouse_pixel = ImPlot::PlotToPixels(mouse_plot.x, mouse_plot.y);
                                    
                                    float dx = screen_pos.x - mouse_pixel.x;
                                    float dy = screen_pos.y - mouse_pixel.y;
                                    float dist_sq = dx * dx + dy * dy;
                                    
                                    if (dist_sq < min_dist_sq) {
                                        min_dist_sq = dist_sq;
                                        closest_point = (int)p;
                                    }
                                }
                                
                                // Check if close enough to hover (within 10 pixels)
                                if (closest_point >= 0 && min_dist_sq < 100.0f) {
                                    hovered_point = closest_point;
                                    
                                    // Show tooltip
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Frame: %d", scatter.frame_indices[closest_point]);
                                    ImGui::Text("X: %.3f", scatter.x_data[closest_point]);
                                    ImGui::Text("Y: %.3f", scatter.y_data[closest_point]);
                                    ImGui::Text("Click to jump to this frame");
                                    ImGui::EndTooltip();
                                    
                                    // Handle click to jump to frame
                                    if (ImGui::IsMouseClicked(0)) {
                                        clicked_frame = scatter.frame_indices[closest_point];
                                        app_state->animation.frame = (double)clicked_frame;
                                    }
                                }
                            }
                        }
                    }
                    
                    ImPlot::EndPlot();
                }
            } else {
                // Show empty plot with instructions
                if (ImPlot::BeginPlot("Property Correlation", ImVec2(-1, -1))) {
                    ImPlot::SetupAxes("X Property", "Y Property");
                    ImPlot::PlotText("Select temporal properties using the property buttons\nto create correlation plot", 0.5, 0.5);
                    ImPlot::EndPlot();
                }
            }
            
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Clear button to the right of the plot
            ImGui::BeginChild("ClearButton", ImVec2(clear_button_width, plot_avail.y - x_button_height), false);
            ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 15.0f);
            if (ImGui::Button("Clear Plot", ImVec2(-1, 30))) {
                x_property_idx = -1;
                y_property_idx = -1;
                // Clear series data
                for (size_t i = 0; i < md_array_size(series); ++i) {
                    md_array_free(series[i].x_data, arena);
                    md_array_free(series[i].y_data, arena);
                    md_array_free(series[i].frame_indices, arena);
                }
                md_array_resize(series, 0, arena);
                error[0] = '\0';
            }
            ImGui::EndChild();
            
            // X-axis property selection button below the plot
            ImGui::SetCursorPosX(y_button_width);
            ImGui::BeginChild("XAxisButton", ImVec2(plot_avail.x - y_button_width - clear_button_width, x_button_height), false);
            
            ImGui::Text("X-Axis Property:");
            ImGui::SameLine();
            
            // X-axis property selection button
            const char* x_button_label = "Select Property";
            char x_display_text[128] = "Select Property";
            
            if (x_property_idx >= 0) {
                const char* x_label = get_display_property_label_by_index(app_state, x_property_idx);
                const char* x_unit = get_display_property_unit_by_index(app_state, x_property_idx, 0);
                if (strlen(x_unit) > 0) {
                    snprintf(x_display_text, sizeof(x_display_text), "%s (%s)", x_label, x_unit);
                } else {
                    snprintf(x_display_text, sizeof(x_display_text), "%s", x_label);
                }
            }
            
            if (ImGui::Button(x_display_text, ImVec2(-1, 0))) {
                ImGui::OpenPopup("X_Property_Selector");
            }
            
            if (ImGui::BeginPopup("X_Property_Selector")) {
                ImGui::Text("Select X-Axis Property:");
                ImGui::Separator();
                
                bool has_temporal_props = false;
                for (int i = 0; i < num_props; ++i) {
                    if (get_display_property_type_by_index(app_state, i) == DisplayPropertyType_Temporal) {
                        has_temporal_props = true;
                        const char* label = get_display_property_label_by_index(app_state, i);
                        ImVec4 color = get_display_property_color_by_index(app_state, i);
                        
                        ImPlot::ItemIcon(color);
                        ImGui::SameLine();
                        
                        if (ImGui::Selectable(label, x_property_idx == i)) {
                            x_property_idx = i;
                            update_scatter_data();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                
                if (!has_temporal_props) {
                    ImGui::Text("No temporal properties available.");
                    ImGui::Text("Define and evaluate properties in the script editor.");
                }
                
                ImGui::EndPopup();
            }
            
            ImGui::EndChild();
            
            // Show error messages if any
            if (strlen(error) > 0) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", error);
            }
        }
        ImGui::End();
    }
};

static Correlation instance = {};