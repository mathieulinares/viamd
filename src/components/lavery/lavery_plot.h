#pragma once

#include <implot.h>
#include <implot_internal.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lavery {

inline bool drag_sequence_landmark(int index, double* position, bool* held) {
    // Finish plot setup before pushing IDs: setup establishes ImPlot's own ID scope.
    ImPlot::GetPlotLimits();
    ImGui::PushOverrideID(ImPlot::GetCurrentPlot()->ID);
    ImGui::PushID("Lavery sequence landmark drag");
    const bool changed=ImPlot::DragLineX(index,position,ImVec4(0,0,0,0),6,
        ImPlotDragToolFlags_NoFit|ImPlotDragToolFlags_Delayed,nullptr,nullptr,held);
    ImGui::PopID();
    ImGui::PopID();
    return changed;
}

inline bool draw_series_row(const char* name, bool* visible, const ImVec4& color, bool* edit) {
    const bool changed=ImGui::Checkbox("##visible",visible);
    ImGui::SameLine();
    ImGui::ColorButton("##colour",color,ImGuiColorEditFlags_NoTooltip,
        ImVec2(ImGui::GetFrameHeight(),ImGui::GetFrameHeight()));
    ImGui::SameLine();
    *edit=ImGui::SmallButton("Edit");
    ImGui::SameLine();
    ImGui::TextUnformatted(name);
    return changed;
}

inline void plot_sequence_heatmap(const char* label, const std::vector<float>& values, int rows,
    const std::vector<double>& edges, double minimum, double maximum) {
    IM_ASSERT(edges.size()>1 && values.size()==(size_t)rows*(edges.size()-1));
    const size_t bins=edges.size()-1;
    std::vector<float> column(rows);
    for (size_t bin=0;bin<bins;++bin) {
        // ImPlot heatmaps enumerate rows from their upper bound; member zero is at y=0.
        for (int row=0;row<rows;++row) column[rows-row-1]=values[(size_t)row*bins+bin];
        ImPlot::PlotHeatmap(label,column.data(),rows,1,minimum,maximum,nullptr,
            ImPlotPoint(edges[bin],-.5),ImPlotPoint(edges[bin+1],rows-.5));
    }
}

inline bool plot_heatmap_landmark(double row, const char* name, const ImVec4& color,
    std::vector<ImVec4>& labels) {
    const auto limits=ImPlot::GetPlotLimits();
    if (!limits.Y.Contains(row)) return false;
    const ImVec2 left=ImPlot::PlotToPixels(limits.X.Min,row),right=ImPlot::PlotToPixels(limits.X.Max,row);
    auto* draw=ImPlot::GetPlotDrawList();
    ImPlot::PushPlotClipRect();
    draw->AddLine(left,right,ImGui::ColorConvertFloat4ToU32(color),3);
    const ImVec2 text=ImGui::CalcTextSize(name);
    const auto pos=ImPlot::GetPlotPos(),size=ImPlot::GetPlotSize();
    const float width=std::min(text.x+10,std::max(1.0f,size.x-8));
    const float height=text.y+6;
    float y=std::clamp(left.y-height-2,pos.y+2,std::max(pos.y+2,pos.y+size.y-height-2));
    const float x=right.x-width-4;
    bool collision=true;
    while (collision) {
        collision=false;
        for (const auto& rect : labels) if (y<rect.w+2 && y+height>rect.y-2) {
            y=rect.w+3; collision=true;
        }
    }
    bool hovered=false;
    if (y+height<pos.y+size.y) {
        labels.push_back(ImVec4(x,y,x+width,y+height));
        draw->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+height),IM_COL32(30,30,30,220),3);
        const ImVec4 clip(x,y,x+width,y+height);
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(x+5,y+3),
            ImGui::ColorConvertFloat4ToU32(color),name,nullptr,0,&clip);
        const auto mouse=ImGui::GetMousePos();
        hovered=mouse.x>=x && mouse.x<=x+width && mouse.y>=y && mouse.y<=y+height;
    }
    ImPlot::PopPlotClipRect();
    return ImPlot::IsPlotHovered() && (hovered || std::fabs(ImGui::GetMousePos().y-left.y)<=5);
}

inline void plot_sequence_bars(const char* label, const std::vector<double>& x,
    const std::vector<double>& y, double width, const ImVec4& color) {
    IM_ASSERT(x.size()==y.size());
    std::vector<double> finite_x,finite_y;
    for (size_t i=0;i<x.size();++i) if (std::isfinite(x[i]) && std::isfinite(y[i])) {
        finite_x.push_back(x[i]); finite_y.push_back(y[i]);
    }
    ImPlot::SetNextFillStyle(color);
    if (finite_x.empty()) ImPlot::PlotDummy(label);
    else ImPlot::PlotBars(label,finite_x.data(),finite_y.data(),(int)finite_x.size(),width);
}

// State-major values at consecutive BP positions; omit unavailable runs before rendering.
inline void plot_sequence_state_bars(const std::vector<const char*>& labels,
    const std::vector<double>& values, size_t positions, double width, double shift) {
    IM_ASSERT(!labels.empty() && values.size()==labels.size()*positions);
    const auto available=[&](size_t p) {
        for (size_t s=0;s<labels.size();++s) if (!std::isfinite(values[s*positions+p])) return false;
        return true;
    };
    bool drawn=false;
    for (size_t first=0;first<positions;) {
        if (!available(first)) { ++first; continue; }
        size_t end=first+1;
        while (end<positions && available(end)) ++end;
        std::vector<double> segment;
        for (size_t s=0;s<labels.size();++s)
            segment.insert(segment.end(),values.begin()+s*positions+first,values.begin()+s*positions+end);
        ImPlot::PlotBarGroups(labels.data(),segment.data(),(int)labels.size(),(int)(end-first),
            width,shift+(double)first,ImPlotBarGroupsFlags_Stacked);
        drawn=true; first=end;
    }
    if (!drawn) for (const char* label : labels) ImPlot::PlotDummy(label);
}

inline void setup_sequence_ticks(double minimum, double maximum) {
    const double step=std::max(1.0,std::ceil((maximum-minimum)/12));
    std::vector<double> ticks;
    for (double value=std::ceil(minimum/step)*step;value<=maximum && ticks.size()<100;value+=step)
        ticks.push_back(value);
    ImPlot::SetupAxisFormat(ImAxis_X1,"%.0f");
    ImPlot::SetupAxisTicks(ImAxis_X1,ticks.data(),(int)ticks.size(),nullptr,false);
}

inline bool plot_sequence_landmark(double position, const char* name, const ImVec4& color,
    std::vector<ImVec4>* label_rects=nullptr, bool draw_line=true) {
    const auto limits=ImPlot::GetPlotLimits();
    if (!limits.X.Contains(position)) return false;
    const ImVec2 top=ImPlot::PlotToPixels(position,limits.Y.Max);
    const ImVec2 bottom=ImPlot::PlotToPixels(position,limits.Y.Min);
    bool label_hovered=false;
    ImPlot::PushPlotClipRect();
    if (draw_line) ImPlot::GetPlotDrawList()->AddLine(top,bottom,ImGui::ColorConvertFloat4ToU32(color),3);
    if (label_rects) {
        const ImVec2 text=ImGui::CalcTextSize(name);
        const ImVec2 plot_pos=ImPlot::GetPlotPos(),plot_size=ImPlot::GetPlotSize();
        const float width=std::min(text.x+10,plot_size.x-8);
        const float left=std::clamp(top.x+4,plot_pos.x+4,plot_pos.x+plot_size.x-width-4);
        const float height=text.y+6;
        float y=top.y+4;
        bool collision=true;
        while (collision) {
            collision=false;
            for (const auto& rect : *label_rects) {
                if (left<rect.z+3 && left+width>rect.x-3 && y<rect.w+3 && y+height>rect.y-3) {
                    y=rect.w+4; collision=true;
                }
            }
        }
        if (y+height<bottom.y) {
            label_rects->push_back(ImVec4(left,y,left+width,y+height));
            const auto mouse=ImGui::GetMousePos();
            label_hovered=mouse.x>=left && mouse.x<=left+width && mouse.y>=y && mouse.y<=y+height;
            auto* draw=ImPlot::GetPlotDrawList();
            draw->AddRectFilled(ImVec2(left,y),ImVec2(left+width,y+height),ImGui::ColorConvertFloat4ToU32(color),3);
            const float luminance=.2126f*color.x+.7152f*color.y+.0722f*color.z;
            const ImVec4 clip(left,y,left+width,y+height);
            draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(left+5,y+3),
                luminance>.5f ? IM_COL32(0,0,0,255) : IM_COL32(255,255,255,255),name,nullptr,0,&clip);
        }
    }
    ImPlot::PopPlotClipRect();
    return ImPlot::IsPlotHovered() && (label_hovered || std::fabs(ImGui::GetMousePos().x-top.x)<=6);
}

inline void plot_timeline_guide(double first, double last, const char* name, const ImVec4& color,
    std::vector<ImVec4>& labels) {
    const auto limits=ImPlot::GetPlotLimits();
    if (last<limits.X.Min || first>limits.X.Max) return;
    if (last>first) {
        ImPlot::PushPlotClipRect();
        ImVec4 shade=color; shade.w=.07f;
        ImPlot::GetPlotDrawList()->AddRectFilled(
            ImPlot::PlotToPixels(std::max(first,limits.X.Min),limits.Y.Max),
            ImPlot::PlotToPixels(std::min(last,limits.X.Max),limits.Y.Min),ImGui::ColorConvertFloat4ToU32(shade));
        ImPlot::PopPlotClipRect();
        plot_sequence_landmark(last,"",color);
    }
    // Keep the period name visible even when its start lies outside the zoomed view.
    plot_sequence_landmark(std::max(first,limits.X.Min),name,color,&labels,first>=limits.X.Min);
}

// ImPlot's band/line renderers can emit nonfinite vertices at NaN gaps.
inline void plot_sequence_trace(const char* label, const std::vector<double>& x, const std::vector<double>& y,
    const std::vector<double>* lower, const std::vector<double>* upper, const ImVec4& color,
    ImPlotMarker marker=ImPlotMarker_Circle, bool scatter=false, float opacity=.18f) {
    IM_ASSERT(x.size()==y.size() && ((lower && upper) || (!lower && !upper)));
    IM_ASSERT(!lower || (lower->size()==x.size() && upper->size()==x.size()));
    const auto finite=[&](size_t i) {
        return std::isfinite(x[i]) && std::isfinite(y[i]) &&
            (!lower || (std::isfinite((*lower)[i]) && std::isfinite((*upper)[i])));
    };
    bool drawn=false;
    for (size_t first=0;first<x.size();) {
        if (!finite(first)) { ++first; continue; }
        size_t end=first+1;
        while (end<x.size() && finite(end)) ++end;
        const int count=(int)(end-first);
        if (lower && count>1) {
            ImPlot::SetNextFillStyle(color,opacity);
            ImPlot::PlotShaded(label,x.data()+first,lower->data()+first,upper->data()+first,count);
        }
        ImPlot::SetNextLineStyle(color,2);
        ImPlot::SetNextMarkerStyle(marker,scatter ? 4 : 3,color,scatter ? 1 : 0,color);
        if (scatter) ImPlot::PlotScatter(label,x.data()+first,y.data()+first,count);
        else ImPlot::PlotLine(label,x.data()+first,y.data()+first,count);
        drawn=true;
        first=end;
    }
    if (!drawn) {
        ImPlot::SetNextLineStyle(color,2);
        ImPlot::PlotDummy(label);
    }
}

}
