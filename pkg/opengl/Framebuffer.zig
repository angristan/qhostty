const Framebuffer = @This();

const std = @import("std");
const c = @import("c.zig").c;
const errors = @import("errors.zig");
const glad = @import("glad.zig");
const Texture = @import("Texture.zig");
const Renderbuffer = @import("Renderbuffer.zig");

id: c.GLuint,

/// Create a single buffer.
pub fn create() !Framebuffer {
    var fbo: c.GLuint = undefined;
    glad.context.GenFramebuffers.?(1, &fbo);
    return .{ .id = fbo };
}

pub fn destroy(v: Framebuffer) void {
    glad.context.DeleteFramebuffers.?(1, &v.id);
}

pub fn bind(v: Framebuffer, target: Target) !Binding {
    // The default framebuffer is not necessarily zero. Read and draw targets
    // can also differ, notably when rendering through QOpenGLWidget.
    const binding: c.GLenum = switch (target) {
        .framebuffer => c.GL_FRAMEBUFFER_BINDING,
        .draw => c.GL_DRAW_FRAMEBUFFER_BINDING,
        .read => c.GL_READ_FRAMEBUFFER_BINDING,
        else => c.GL_FRAMEBUFFER_BINDING,
    };
    var current: c.GLint = undefined;
    glad.context.GetIntegerv.?(binding, &current);
    glad.context.BindFramebuffer.?(@intFromEnum(target), v.id);
    return .{ .target = target, .previous = @intCast(current) };
}

/// Enum for possible binding targets.
pub const Target = enum(c_uint) {
    framebuffer = c.GL_FRAMEBUFFER,
    draw = c.GL_DRAW_FRAMEBUFFER,
    read = c.GL_READ_FRAMEBUFFER,
    _,
};

pub const Attachment = enum(c_uint) {
    color0 = c.GL_COLOR_ATTACHMENT0,
    depth = c.GL_DEPTH_ATTACHMENT,
    stencil = c.GL_STENCIL_ATTACHMENT,
    depth_stencil = c.GL_DEPTH_STENCIL_ATTACHMENT,
    _,
};

pub const Status = enum(c_uint) {
    complete = c.GL_FRAMEBUFFER_COMPLETE,
    undefined = c.GL_FRAMEBUFFER_UNDEFINED,
    incomplete_attachment = c.GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
    incomplete_missing_attachment = c.GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
    incomplete_draw_buffer = c.GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER,
    incomplete_read_buffer = c.GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER,
    unsupported = c.GL_FRAMEBUFFER_UNSUPPORTED,
    incomplete_multisample = c.GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE,
    incomplete_layer_targets = c.GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS,
    _,
};

pub const Binding = struct {
    target: Target,
    previous: c.GLuint,

    pub fn unbind(self: Binding) void {
        glad.context.BindFramebuffer.?(
            @intFromEnum(self.target),
            self.previous,
        );
    }

    pub fn texture2D(
        self: Binding,
        attachment: Attachment,
        textarget: Texture.Target,
        texture: Texture,
        level: c.GLint,
    ) !void {
        glad.context.FramebufferTexture2D.?(
            @intFromEnum(self.target),
            @intFromEnum(attachment),
            @intFromEnum(textarget),
            texture.id,
            level,
        );
        try errors.getError();
    }

    pub fn renderbuffer(
        self: Binding,
        attachment: Attachment,
        buffer: Renderbuffer,
    ) !void {
        glad.context.FramebufferRenderbuffer.?(
            @intFromEnum(self.target),
            @intFromEnum(attachment),
            c.GL_RENDERBUFFER,
            buffer.id,
        );
        try errors.getError();
    }

    pub fn drawBuffers(
        self: Binding,
        bufs: []Attachment,
    ) !void {
        _ = self;
        glad.context.DrawBuffers.?(@intCast(bufs.len), bufs.ptr);
        try errors.getError();
    }

    pub fn checkStatus(self: Binding) Status {
        return @enumFromInt(glad.context.CheckFramebufferStatus.?(@intFromEnum(self.target)));
    }
};
