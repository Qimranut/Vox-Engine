#include "EditorUI.hpp"
#include "text/text_layout.hpp"
#include <GL/glew.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace vox::editor {
void EditorUI::pickObject(const engine::SceneView& camera, float x, float y, float width, float height) {
    if (!viewportHovered_ || playing_ || pendingPlay_ || !scene_->loaded() ||
        !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
        ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    // Gizmo handles own their click, even when they overlap another object.
    if (selected_ && (draft_.hasTransform || (draft_.hasText && draft_.text.screenSpace)) &&
        (ImGuizmo::IsOver() || ImGuizmo::IsUsing())) return;

    const auto mouse = ImGui::GetMousePos();
    const float u = (mouse.x - x) / width, v = (mouse.y - y) / height;
    if (u < 0 || u >= 1 || v < 0 || v >= 1) return;
    const auto document = scene_->snapshot();
    const auto hitsText = [&](const SceneObject& object, glm::vec2 point) {
        if (object.text.font.empty()) return false;
        try {
            const auto font = resources_->load(object.text.font);
            if (!font->font) return false;
            const auto vertices = text::layout(*font->font, object.text);
            for (size_t i = 0; i + 5 < vertices.size(); i += 6)
                if (point.x >= vertices[i].x && point.x <= vertices[i+2].x &&
                    point.y >= vertices[i].y && point.y <= vertices[i+2].y) return true;
        } catch (const std::exception&) { /* An unavailable font has nothing to pick. */ }
        return false;
    };
    uint32_t screenPicked = 0;
    for (const auto& object : document.objects)
        if (object.hasText && object.text.screenSpace && (!object.hasVisibility || object.visible) &&
            hitsText(object, glm::vec2(u * viewportWidth_, v * viewportHeight_) - object.text.screenPosition))
            screenPicked = object.id;
    if (screenPicked) { select(screenPicked); return; }
    const auto inverseViewProjection = glm::inverse(glm::make_mat4(camera.projection.data()) * glm::make_mat4(camera.view.data()));
    const auto unproject = [&](float depth) {
        const auto point = inverseViewProjection * glm::vec4(2*u-1, 1-2*v, depth, 1);
        return glm::vec3(point) / point.w;
    };
    const auto origin = unproject(-1);
    const auto direction = glm::normalize(unproject(1) - origin);

    // Menu images are XY quads, drawn in Z/entity order without depth writes.
    // Match that geometry and order instead of testing the old cube thickness.
    if (scene_->is2D()) {
        const SceneObject* picked = nullptr;
        for (const auto& object : document.objects) {
            if (object.hasText || !object.hasTransform || !object.hasVisibility || !object.visible) continue;
            const auto& t = object.transform;
            const auto matrix = glm::translate(glm::mat4(1), t.pos_) * glm::mat4_cast(t.rot_) * glm::scale(glm::mat4(1), t.scale_);
            if (std::abs(glm::determinant(matrix)) < 1e-12f) continue;
            const auto inverse = glm::inverse(matrix);
            const auto o = glm::vec3(inverse * glm::vec4(origin, 1));
            const auto d = glm::vec3(inverse * glm::vec4(direction, 0));
            if (std::abs(d.z) < 1e-8f) continue;
            const float distance = -o.z / d.z;
            const auto hit = o + distance * d;
            if (distance < 0 || std::abs(hit.x) > .5f || std::abs(hit.y) > .5f) continue;
            if (!picked || t.pos_.z > picked->transform.pos_.z ||
                (t.pos_.z == picked->transform.pos_.z && entt::to_integral(scene_->entity(object.id)) > entt::to_integral(scene_->entity(picked->id))))
                picked = &object;
        }
        if (picked) { select(picked->id); return; }
    }

    // Respect the actual rendered depth so a cube behind terrain cannot be picked.
    GLint previousReadFramebuffer{};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, viewportFramebuffer_);
    float depth = 1;
    const int px = std::clamp(static_cast<int>(u * viewportWidth_), 0, viewportWidth_-1);
    const int py = std::clamp(static_cast<int>((1-v) * viewportHeight_), 0, viewportHeight_-1);
    glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    const float visibleDistance = glm::length(unproject(2*depth-1) - origin);
    float nearest = visibleDistance + std::max(0.01f, visibleDistance * 0.001f);
    uint32_t picked = 0;
    for (const auto& object : document.objects) {
        if (scene_->is2D() && !object.hasText) continue;
        if (!object.hasTransform || (object.hasVisibility && !object.visible) ||
            (!object.hasText && !object.hasVisibility) || (object.hasText && object.text.screenSpace)) continue;
        const auto& t = object.transform;
        const auto world = glm::translate(glm::mat4(1), t.pos_) * glm::mat4_cast(t.rot_) * glm::scale(glm::mat4(1), t.scale_);
        if (std::abs(glm::determinant(world)) < 1e-12f) continue;
        const auto inverse = glm::inverse(world);
        const auto localOrigin = glm::vec3(inverse * glm::vec4(origin, 1));
        // Do not normalize: the intersection parameter remains world distance.
        const auto localDirection = glm::vec3(inverse * glm::vec4(direction, 0));
        if (object.hasText) {
            if (std::abs(localDirection.z) > 1e-8f) {
                const float distance = -localOrigin.z / localDirection.z;
                const auto point = localOrigin + distance * localDirection;
                if (distance >= 0 && distance < nearest && hitsText(object, {point.x, -point.y})) {
                    nearest = distance; picked = object.id;
                }
            }
            continue;
        }
        float entry = 0, exit = nearest;
        bool hit = true;
        // Renderer::renderEntities currently draws the unit cube for every scene
        // object. Intersect that same geometry, including rotation and scale.
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(localDirection[axis]) < 1e-8f) {
                if (localOrigin[axis] < -0.5f || localOrigin[axis] > 0.5f) hit = false;
            } else {
                float a = (-0.5f-localOrigin[axis]) / localDirection[axis];
                float b = (0.5f-localOrigin[axis]) / localDirection[axis];
                if (a > b) std::swap(a,b);
                entry = std::max(entry,a); exit = std::min(exit,b);
                if (entry > exit) hit = false;
            }
        }
        if (hit && entry < nearest) { nearest = entry; picked = object.id; }
    }
    select(picked);
}

void EditorUI::gizmoToolbar() {
    const bool screenText = selected_ && draft_.hasText && draft_.text.screenSpace;
    ImGui::BeginDisabled(playing_ || pendingPlay_ || gizmoDragging_);
    const char* tools[]{"Move (1)", "Rotate (2)", "Scale (3)"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        ImGui::BeginDisabled(screenText && i != 0);
        if (ImGui::Selectable(tools[i], (screenText ? 0 : gizmoOperation_) == i, 0,
                ImVec2(ImGui::CalcTextSize(tools[i]).x + 12, ImGui::GetFrameHeight())))
            gizmoOperation_ = i;
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Options")) ImGui::OpenPopup("Gizmo options");
    if (ImGui::BeginPopup("Gizmo options")) {
        ImGui::TextUnformatted("Transform space");
        ImGui::BeginDisabled(screenText || gizmoOperation_ == 2);
        if (ImGui::RadioButton("World", !gizmoLocal_)) gizmoLocal_ = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", gizmoLocal_)) gizmoLocal_ = true;
        ImGui::EndDisabled();
        if (gizmoOperation_ == 2) ImGui::TextDisabled("Scale follows the object's local axes.");
        ImGui::Separator();
        ImGui::Checkbox("Snap", &gizmoSnap_);
        ImGui::SetNextItemWidth(130);
        ImGui::DragFloat(screenText ? "Move step (pixels)" : "Move step", &gizmoMoveSnap_, 0.1f, 0.001f, 1000.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(130);
        ImGui::DragFloat("Rotation degrees", &gizmoRotateSnap_, 1.f, 0.1f, 180.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(130);
        ImGui::DragFloat("Scale step", &gizmoScaleSnap_, 0.01f, 0.001f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("Hold Ctrl to snap temporarily.");
        ImGui::TextDisabled("Drag an axis, plane, ring or centre handle.");
        ImGui::EndPopup();
    }
    ImGui::Checkbox("Lock X", &gizmoAxisLocked_[0]);
    ImGui::SameLine(); ImGui::Checkbox("Lock Y", &gizmoAxisLocked_[1]);
    ImGui::SameLine();
    ImGui::BeginDisabled(screenText);
    ImGui::Checkbox("Lock Z", &gizmoAxisLocked_[2]);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Locked Transform values stay unchanged during gizmo edits. Position locks use world coordinates.");
    ImGui::EndDisabled();
    const bool positionLocked = gizmoOperation_ == 0 && (gizmoAxisLocked_[0] || gizmoAxisLocked_[1] || gizmoAxisLocked_[2]);
    ImGui::TextDisabled("%s", playing_ ? "Pause to edit transforms" :
        !selected_ ? "Click an object in the scene or Hierarchy" :
        screenText ? "Screen pixels | Drag arrows or centre to move text" :
        positionLocked ? "World axes | Position locks active" :
        gizmoOperation_ == 2 || gizmoLocal_ ? "Local axes | X red / Y green / Z blue" : "World axes | X red / Y green / Z blue");
    ImGui::Separator();
}


void EditorUI::gizmo(const engine::SceneView& camera, float x, float y, float width, float height) {
    if (playing_ || pendingPlay_ || !selected_ || !scene_->loaded()) return;
    const auto before = scene_->snapshot();
    const auto found = std::find_if(before.objects.begin(), before.objects.end(),
        [&](const SceneObject& object) { return object.id == selected_; });
    if (found == before.objects.end()) return;
    const bool screenText = found->hasText && found->text.screenSpace;
    if ((!screenText && !found->hasTransform) || width <= 0 || height <= 0 || viewportWidth_ <= 0 || viewportHeight_ <= 0) return;

    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (viewportHovered_ && !gizmoDragging_ && !popup && !ImGui::IsAnyItemActive() &&
        !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) gizmoOperation_ = 0;
        if (!screenText && ImGui::IsKeyPressed(ImGuiKey_2, false)) gizmoOperation_ = 1;
        if (!screenText && ImGui::IsKeyPressed(ImGuiKey_3, false)) gizmoOperation_ = 2;
    }
    auto object = *found;
    const auto& transform = found->transform;
    glm::mat4 matrix = glm::translate(glm::mat4(1), transform.pos_) *
        glm::mat4_cast(transform.rot_) * glm::scale(glm::mat4(1), transform.scale_);
    
    const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    
    // A screen-space text anchor lives in viewport pixels, independent of Transform
    // and the scene camera. The image rect maps those pixels onto the displayed panel.
    const auto screenView = glm::translate(glm::mat4(1), glm::vec3(0, 0, -1));
    const auto screenProjection = glm::ortho(0.f, static_cast<float>(viewportWidth_),
        static_cast<float>(viewportHeight_), 0.f, .1f, 10.f);
    if (screenText) matrix = glm::translate(glm::mat4(1), glm::vec3(found->text.screenPosition, 0));
    const int activeOperation = screenText ? 0 : gizmoOperation_;
    const bool anyLocked = gizmoAxisLocked_[0] || gizmoAxisLocked_[1] || (!screenText && gizmoAxisLocked_[2]);
    const bool allLocked = scene_->is2D() && !screenText && gizmoOperation_ == 1
        ? gizmoAxisLocked_[2]
        : gizmoAxisLocked_[0] && gizmoAxisLocked_[1] && (screenText || scene_->is2D() || gizmoAxisLocked_[2]);
    
    // Pass the full operation flag so the center screen-space handle remains visible.
    // The visual handles for locked axes will remain, but pulling them will not move the object 
    // thanks to your post-manipulation override below.
    const auto operation = scene_->is2D() && !screenText && activeOperation == 1 ? ImGuizmo::ROTATE_Z : operations[activeOperation];
    
    // Transform position is world-space, regardless of the orientation of the handles.
    const bool local = !scene_->is2D() && !screenText && ((gizmoLocal_ && !(activeOperation == 0 && anyLocked)) || activeOperation == 2);
    const float step = activeOperation == 0 ? gizmoMoveSnap_ : activeOperation == 1 ? gizmoRotateSnap_ : gizmoScaleSnap_;
    const float snap[]{step, step, step};
    
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(x, y, width, height);
    ImGuizmo::SetOrthographic(screenText || camera.orthographic);
    ImGuizmo::Enable(!allLocked && !popup && !ImGui::IsMouseDown(ImGuiMouseButton_Right));
    ImGui::GetWindowDrawList()->PushClipRect(ImVec2(x,y), ImVec2(x+width,y+height), true);
    
    const bool changed = ImGuizmo::Manipulate(screenText ? glm::value_ptr(screenView) : camera.view.data(),
        screenText ? glm::value_ptr(screenProjection) : camera.projection.data(),
        operation, local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
        glm::value_ptr(matrix), nullptr, gizmoSnap_ || ImGui::GetIO().KeyCtrl ? snap : nullptr);
        
    ImGui::GetWindowDrawList()->PopClipRect();
    gizmoDragging_ = ImGuizmo::IsUsing();
    
    if (!changed || allLocked) return;

    // Only write the edited property: avoid Euler round trips, loss of negative
    // scale signs, or drift in untouched properties during repeated drags.
    if (screenText) {
        object.text.screenPosition = glm::vec2(matrix[3]);
        for (int axis = 0; axis < 2; ++axis)
            if (gizmoAxisLocked_[axis]) object.text.screenPosition[axis] = found->text.screenPosition[axis];
        if (!std::isfinite(object.text.screenPosition.x) || !std::isfinite(object.text.screenPosition.y) ||
            object.text.screenPosition == found->text.screenPosition) return;
    }
    else if (gizmoOperation_ == 0) object.transform.pos_ = glm::vec3(matrix[3]);
    else if (gizmoOperation_ == 1) {
        glm::mat3 rotation;
        for (int axis = 0; axis < 3; ++axis)
            rotation[axis] = glm::vec3(matrix[axis]) / transform.scale_[axis];
        object.transform.rot_ = glm::normalize(glm::quat_cast(rotation));
    } else {
        for (int axis = 0; axis < 3; ++axis)
            object.transform.scale_[axis] = std::copysign(std::max(0.0001f,
                glm::length(glm::vec3(matrix[axis]))), transform.scale_[axis]);
    }
    
    // Forcefully overwrite the modified matrix data for axes that are supposed to be locked.
    if (!screenText && anyLocked) {
        auto angles = glm::eulerAngles(object.transform.rot_);
        const auto previousAngles = glm::eulerAngles(transform.rot_);
        for (int axis = 0; axis < 3; ++axis) {
            if (!gizmoAxisLocked_[axis]) continue;
            object.transform.pos_[axis] = transform.pos_[axis];
            object.transform.scale_[axis] = transform.scale_[axis];
            angles[axis] = previousAngles[axis];
        }
        if (gizmoOperation_ == 1) object.transform.rot_ = glm::quat(angles);
    }
    
    if (scene_->is2D() && !screenText) {
        object.transform.pos_.z = transform.pos_.z;
        object.transform.scale_.z = transform.scale_.z;
    }
    attempt([&] {
        scene_->update(object);
        if (!gizmoRecorded_) {
            pushUndo({ before, 0 });
            if (undo_.size() > 50) undo_.erase(undo_.begin());
            redo_.clear();
            gizmoRecorded_ = true;
        }
        select(selected_);
    });
}

}
