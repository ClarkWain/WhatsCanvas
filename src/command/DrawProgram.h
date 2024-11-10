#pragma once

#include "DrawData.h"
#include "opengl/GLProgram.h" 

#include "render/RenderContext.h"

class DrawPointsProgram 
{
public:
    // 删除拷贝构造和赋值运算符
    DrawPointsProgram(const DrawPointsProgram&) = delete;
    DrawPointsProgram& operator=(const DrawPointsProgram&) = delete;
    
    // 获取单例实例的静态方法
    static DrawPointsProgram* getInstance() {
        if (instance_ == nullptr) {
            instance_ = new DrawPointsProgram();
        }
        return instance_;
    }

    ~DrawPointsProgram();

    void initialize();
    void release();

    void draw(const RenderContext &context, const DrawPointsData &data);

private:
    // 构造函数改为私有
    DrawPointsProgram();
    
    // 静态实例指针
    static DrawPointsProgram* instance_;
    
    GLProgram* program_ =  nullptr;
    unsigned int VAO_ = -1;
    unsigned int VBO_ = -1;

    bool initialized_ = false;

    int maxPoints_ = 1000;
};
