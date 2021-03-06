//
//  MetavoxelSystem.cpp
//  interface/src
//
//  Created by Andrzej Kapolka on 12/10/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QMutexLocker>
#include <QtDebug>

#include <glm/gtx/transform.hpp>

#include <SharedUtil.h>

#include <MetavoxelUtil.h>
#include <ScriptCache.h>

#include "Application.h"
#include "MetavoxelSystem.h"
#include "renderer/Model.h"

REGISTER_META_OBJECT(SphereRenderer)
REGISTER_META_OBJECT(StaticModelRenderer)

ProgramObject MetavoxelSystem::_program;
int MetavoxelSystem::_pointScaleLocation;

MetavoxelSystem::MetavoxelSystem() :
    _simulateVisitor(_points),
    _buffer(QOpenGLBuffer::VertexBuffer) {
}

void MetavoxelSystem::init() {
    if (!_program.isLinked()) {
        _program.addShaderFromSourceFile(QGLShader::Vertex, Application::resourcesPath() + "shaders/metavoxel_point.vert");
        _program.link();
       
        _pointScaleLocation = _program.uniformLocation("pointScale");
    }
    _buffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    _buffer.create();
    
    connect(NodeList::getInstance(), SIGNAL(nodeAdded(SharedNodePointer)), SLOT(maybeAttachClient(const SharedNodePointer&)));
}

SharedObjectPointer MetavoxelSystem::findFirstRaySpannerIntersection(
        const glm::vec3& origin, const glm::vec3& direction, const AttributePointer& attribute, float& distance) {
   SharedObjectPointer closestSpanner;
   float closestDistance = FLT_MAX;
   foreach (const SharedNodePointer& node, NodeList::getInstance()->getNodeHash()) {
        if (node->getType() == NodeType::MetavoxelServer) {
            QMutexLocker locker(&node->getMutex());
            MetavoxelClient* client = static_cast<MetavoxelClient*>(node->getLinkedData());
            if (client) {
                float clientDistance;
                SharedObjectPointer clientSpanner = client->getData().findFirstRaySpannerIntersection(
                    origin, direction, attribute, clientDistance);
                if (clientSpanner && clientDistance < closestDistance) {
                    closestSpanner = clientSpanner;
                    closestDistance = clientDistance;
                }
            }
        }
    }
    if (closestSpanner) {
        distance = closestDistance;
    }
    return closestSpanner;
}

void MetavoxelSystem::applyEdit(const MetavoxelEditMessage& edit, bool reliable) {
    foreach (const SharedNodePointer& node, NodeList::getInstance()->getNodeHash()) {
        if (node->getType() == NodeType::MetavoxelServer) {
            QMutexLocker locker(&node->getMutex());
            MetavoxelClient* client = static_cast<MetavoxelClient*>(node->getLinkedData());
            if (client) {
                client->applyEdit(edit, reliable);
            }
        }
    }
}

void MetavoxelSystem::simulate(float deltaTime) {
    // simulate the clients
    _points.clear();
    _simulateVisitor.setDeltaTime(deltaTime);
    _simulateVisitor.setOrder(-Application::getInstance()->getViewFrustum()->getDirection());
    foreach (const SharedNodePointer& node, NodeList::getInstance()->getNodeHash()) {
        if (node->getType() == NodeType::MetavoxelServer) {
            QMutexLocker locker(&node->getMutex());
            MetavoxelClient* client = static_cast<MetavoxelClient*>(node->getLinkedData());
            if (client) {
                client->simulate(deltaTime);
                client->guide(_simulateVisitor);
            }
        }
    }
    
    _buffer.bind();
    int bytes = _points.size() * sizeof(Point);
    if (_buffer.size() < bytes) {
        _buffer.allocate(_points.constData(), bytes);
    } else {
        _buffer.write(0, _points.constData(), bytes);
    }
    _buffer.release();
}

void MetavoxelSystem::render() {
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int VIEWPORT_WIDTH_INDEX = 2;
    const int VIEWPORT_HEIGHT_INDEX = 3;
    float viewportWidth = viewport[VIEWPORT_WIDTH_INDEX];
    float viewportHeight = viewport[VIEWPORT_HEIGHT_INDEX];
    float viewportDiagonal = sqrtf(viewportWidth*viewportWidth + viewportHeight*viewportHeight);
    float worldDiagonal = glm::distance(Application::getInstance()->getViewFrustum()->getNearBottomLeft(),
        Application::getInstance()->getViewFrustum()->getNearTopRight());

    _program.bind();
    _program.setUniformValue(_pointScaleLocation, viewportDiagonal *
        Application::getInstance()->getViewFrustum()->getNearClip() / worldDiagonal);
        
    _buffer.bind();

    Point* pt = 0;
    glVertexPointer(4, GL_FLOAT, sizeof(Point), &pt->vertex);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Point), &pt->color);
    glNormalPointer(GL_BYTE, sizeof(Point), &pt->normal);    

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE_ARB);

    glDrawArrays(GL_POINTS, 0, _points.size());
    
    glDisable(GL_VERTEX_PROGRAM_POINT_SIZE_ARB);
    
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    
    _buffer.release();
    
    _program.release();
    
    foreach (const SharedNodePointer& node, NodeList::getInstance()->getNodeHash()) {
        if (node->getType() == NodeType::MetavoxelServer) {
            QMutexLocker locker(&node->getMutex());
            MetavoxelClient* client = static_cast<MetavoxelClient*>(node->getLinkedData());
            if (client) {
                client->guide(_renderVisitor);
            }
        }
    }
}

void MetavoxelSystem::maybeAttachClient(const SharedNodePointer& node) {
    if (node->getType() == NodeType::MetavoxelServer) {
        QMutexLocker locker(&node->getMutex());
        node->setLinkedData(new MetavoxelClient(NodeList::getInstance()->nodeWithUUID(node->getUUID())));
    }
}

MetavoxelSystem::SimulateVisitor::SimulateVisitor(QVector<Point>& points) :
    SpannerVisitor(QVector<AttributePointer>() << AttributeRegistry::getInstance()->getSpannersAttribute(),
        QVector<AttributePointer>(), QVector<AttributePointer>() << AttributeRegistry::getInstance()->getColorAttribute() <<
            AttributeRegistry::getInstance()->getNormalAttribute() <<
            AttributeRegistry::getInstance()->getSpannerColorAttribute() <<
            AttributeRegistry::getInstance()->getSpannerNormalAttribute()),
    _points(points) {
}

bool MetavoxelSystem::SimulateVisitor::visit(Spanner* spanner, const glm::vec3& clipMinimum, float clipSize) {
    spanner->getRenderer()->simulate(_deltaTime);
    return true;
}

int MetavoxelSystem::SimulateVisitor::visit(MetavoxelInfo& info) {
    SpannerVisitor::visit(info);

    if (!info.isLeaf) {
        return _order;
    }
    QRgb color = info.inputValues.at(0).getInlineValue<QRgb>();
    QRgb normal = info.inputValues.at(1).getInlineValue<QRgb>();
    quint8 alpha = qAlpha(color);
    if (!info.isLODLeaf) {
        if (alpha > 0) {
            Point point = { glm::vec4(info.minimum + glm::vec3(info.size, info.size, info.size) * 0.5f, info.size),
                { quint8(qRed(color)), quint8(qGreen(color)), quint8(qBlue(color)), alpha }, 
                { quint8(qRed(normal)), quint8(qGreen(normal)), quint8(qBlue(normal)) } };
            _points.append(point);
        }
    } else {
        QRgb spannerColor = info.inputValues.at(2).getInlineValue<QRgb>();
        QRgb spannerNormal = info.inputValues.at(3).getInlineValue<QRgb>();
        quint8 spannerAlpha = qAlpha(spannerColor);
        if (spannerAlpha > 0) {
            if (alpha > 0) {
                Point point = { glm::vec4(info.minimum + glm::vec3(info.size, info.size, info.size) * 0.5f, info.size),
                    { quint8(qRed(spannerColor)), quint8(qGreen(spannerColor)), quint8(qBlue(spannerColor)), spannerAlpha }, 
                    { quint8(qRed(spannerNormal)), quint8(qGreen(spannerNormal)), quint8(qBlue(spannerNormal)) } };
                _points.append(point);
                
            } else {
                Point point = { glm::vec4(info.minimum + glm::vec3(info.size, info.size, info.size) * 0.5f, info.size),
                    { quint8(qRed(spannerColor)), quint8(qGreen(spannerColor)), quint8(qBlue(spannerColor)), spannerAlpha }, 
                    { quint8(qRed(spannerNormal)), quint8(qGreen(spannerNormal)), quint8(qBlue(spannerNormal)) } };
                _points.append(point);
            }
        } else if (alpha > 0) {
            Point point = { glm::vec4(info.minimum + glm::vec3(info.size, info.size, info.size) * 0.5f, info.size),
                { quint8(qRed(color)), quint8(qGreen(color)), quint8(qBlue(color)), alpha }, 
                { quint8(qRed(normal)), quint8(qGreen(normal)), quint8(qBlue(normal)) } };
            _points.append(point);
        }
    }
    return STOP_RECURSION;
}

MetavoxelSystem::RenderVisitor::RenderVisitor() :
    SpannerVisitor(QVector<AttributePointer>() << AttributeRegistry::getInstance()->getSpannersAttribute(),
        QVector<AttributePointer>() << AttributeRegistry::getInstance()->getSpannerMaskAttribute()) {
}

bool MetavoxelSystem::RenderVisitor::visit(Spanner* spanner, const glm::vec3& clipMinimum, float clipSize) {
    spanner->getRenderer()->render(1.0f, SpannerRenderer::DEFAULT_MODE, clipMinimum, clipSize);
    return true;
}

MetavoxelClient::MetavoxelClient(const SharedNodePointer& node) :
    _node(node),
    _sequencer(byteArrayWithPopulatedHeader(PacketTypeMetavoxelData)) {
    
    connect(&_sequencer, SIGNAL(readyToWrite(const QByteArray&)), SLOT(sendData(const QByteArray&)));
    connect(&_sequencer, SIGNAL(readyToRead(Bitstream&)), SLOT(readPacket(Bitstream&)));
    connect(&_sequencer, SIGNAL(sendAcknowledged(int)), SLOT(clearSendRecordsBefore(int)));
    connect(&_sequencer, SIGNAL(receiveAcknowledged(int)), SLOT(clearReceiveRecordsBefore(int)));
    
    // insert the baseline send record
    SendRecord sendRecord = { 0 };
    _sendRecords.append(sendRecord);
    
    // insert the baseline receive record
    ReceiveRecord receiveRecord = { 0, _data };
    _receiveRecords.append(receiveRecord);
}

MetavoxelClient::~MetavoxelClient() {
    // close the session
    Bitstream& out = _sequencer.startPacket();
    out << QVariant::fromValue(CloseSessionMessage());
    _sequencer.endPacket();
}

static MetavoxelLOD getLOD() {
    const float FIXED_LOD_THRESHOLD = 0.01f;
    return MetavoxelLOD(Application::getInstance()->getCamera()->getPosition(), FIXED_LOD_THRESHOLD);
}

void MetavoxelClient::guide(MetavoxelVisitor& visitor) {
    visitor.setLOD(getLOD());
    _data.guide(visitor);
}

void MetavoxelClient::applyEdit(const MetavoxelEditMessage& edit, bool reliable) {
    if (reliable) {
        _sequencer.getReliableOutputChannel()->sendMessage(QVariant::fromValue(edit));
    
    } else {
        // apply immediately to local tree
        edit.apply(_data, _sequencer.getWeakSharedObjectHash());

        // start sending it out
        _sequencer.sendHighPriorityMessage(QVariant::fromValue(edit));
    }
}

void MetavoxelClient::simulate(float deltaTime) {
    Bitstream& out = _sequencer.startPacket();
    
    ClientStateMessage state = { getLOD() };
    out << QVariant::fromValue(state);
    _sequencer.endPacket();
    
    // record the send
    SendRecord record = { _sequencer.getOutgoingPacketNumber(), state.lod };
    _sendRecords.append(record);
}

int MetavoxelClient::parseData(const QByteArray& packet) {
    // process through sequencer
    QMetaObject::invokeMethod(&_sequencer, "receivedDatagram", Q_ARG(const QByteArray&, packet));
    Application::getInstance()->getBandwidthMeter()->inputStream(BandwidthMeter::METAVOXELS).updateValue(packet.size());
    return packet.size();
}

void MetavoxelClient::sendData(const QByteArray& data) {
    NodeList::getInstance()->writeDatagram(data, _node);
    Application::getInstance()->getBandwidthMeter()->outputStream(BandwidthMeter::METAVOXELS).updateValue(data.size());
}

void MetavoxelClient::readPacket(Bitstream& in) {
    QVariant message;
    in >> message;
    handleMessage(message, in);
    
    // record the receipt
    ReceiveRecord record = { _sequencer.getIncomingPacketNumber(), _data, _sendRecords.first().lod };
    _receiveRecords.append(record);
    
    // reapply local edits
    foreach (const DatagramSequencer::HighPriorityMessage& message, _sequencer.getHighPriorityMessages()) {
        if (message.data.userType() == MetavoxelEditMessage::Type) {
            message.data.value<MetavoxelEditMessage>().apply(_data, _sequencer.getWeakSharedObjectHash());
        }
    }
}

void MetavoxelClient::clearSendRecordsBefore(int index) {
    _sendRecords.erase(_sendRecords.begin(), _sendRecords.begin() + index + 1);
}

void MetavoxelClient::clearReceiveRecordsBefore(int index) {
    _receiveRecords.erase(_receiveRecords.begin(), _receiveRecords.begin() + index + 1);
}

void MetavoxelClient::handleMessage(const QVariant& message, Bitstream& in) {
    int userType = message.userType();
    if (userType == MetavoxelDeltaMessage::Type) {
        _data.readDelta(_receiveRecords.first().data, _receiveRecords.first().lod, in, _sendRecords.first().lod);
        
    } else if (userType == QMetaType::QVariantList) {
        foreach (const QVariant& element, message.toList()) {
            handleMessage(element, in);
        }
    }
}

static void enableClipPlane(GLenum plane, float x, float y, float z, float w) {
    GLdouble coefficients[] = { x, y, z, w };
    glClipPlane(plane, coefficients);
    glEnable(plane);
}

void ClippedRenderer::render(float alpha, Mode mode, const glm::vec3& clipMinimum, float clipSize) {
    if (clipSize == 0.0f) {
        renderUnclipped(alpha, mode);
        return;
    }
    enableClipPlane(GL_CLIP_PLANE0, -1.0f, 0.0f, 0.0f, clipMinimum.x + clipSize);
    enableClipPlane(GL_CLIP_PLANE1, 1.0f, 0.0f, 0.0f, -clipMinimum.x);
    enableClipPlane(GL_CLIP_PLANE2, 0.0f, -1.0f, 0.0f, clipMinimum.y + clipSize);
    enableClipPlane(GL_CLIP_PLANE3, 0.0f, 1.0f, 0.0f, -clipMinimum.y);
    enableClipPlane(GL_CLIP_PLANE4, 0.0f, 0.0f, -1.0f, clipMinimum.z + clipSize);
    enableClipPlane(GL_CLIP_PLANE5, 0.0f, 0.0f, 1.0f, -clipMinimum.z);
    
    renderUnclipped(alpha, mode);
    
    glDisable(GL_CLIP_PLANE0);
    glDisable(GL_CLIP_PLANE1);
    glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3);
    glDisable(GL_CLIP_PLANE4);
    glDisable(GL_CLIP_PLANE5);
}

SphereRenderer::SphereRenderer() {
}

void SphereRenderer::render(float alpha, Mode mode, const glm::vec3& clipMinimum, float clipSize) {
    if (clipSize == 0.0f) {
        renderUnclipped(alpha, mode);
        return;
    }
    // slight performance optimization: don't render if clip bounds are entirely within sphere
    Sphere* sphere = static_cast<Sphere*>(parent());
    Box clipBox(clipMinimum, clipMinimum + glm::vec3(clipSize, clipSize, clipSize));
    for (int i = 0; i < Box::VERTEX_COUNT; i++) {
        const float CLIP_PROPORTION = 0.95f;
        if (glm::distance(sphere->getTranslation(), clipBox.getVertex(i)) >= sphere->getScale() * CLIP_PROPORTION) {
            ClippedRenderer::render(alpha, mode, clipMinimum, clipSize);
            return;
        }
    }
}

void SphereRenderer::renderUnclipped(float alpha, Mode mode) {
    Sphere* sphere = static_cast<Sphere*>(parent());
    const QColor& color = sphere->getColor();
    glColor4f(color.redF(), color.greenF(), color.blueF(), color.alphaF() * alpha);
    
    glPushMatrix();
    const glm::vec3& translation = sphere->getTranslation();
    glTranslatef(translation.x, translation.y, translation.z);
    glm::quat rotation = sphere->getRotation();
    glm::vec3 axis = glm::axis(rotation);
    glRotatef(glm::angle(rotation), axis.x, axis.y, axis.z);
    
    glutSolidSphere(sphere->getScale(), 10, 10);
    
    glPopMatrix();
}

StaticModelRenderer::StaticModelRenderer() :
    _model(new Model(this)) {
}

void StaticModelRenderer::init(Spanner* spanner) {
    _model->init();
    
    StaticModel* staticModel = static_cast<StaticModel*>(spanner);
    applyTranslation(staticModel->getTranslation());
    applyRotation(staticModel->getRotation());
    applyScale(staticModel->getScale());
    applyURL(staticModel->getURL());
    
    connect(spanner, SIGNAL(translationChanged(const glm::vec3&)), SLOT(applyTranslation(const glm::vec3&)));
    connect(spanner, SIGNAL(rotationChanged(const glm::quat&)), SLOT(applyRotation(const glm::quat&)));
    connect(spanner, SIGNAL(scaleChanged(float)), SLOT(applyScale(float)));
    connect(spanner, SIGNAL(urlChanged(const QUrl&)), SLOT(applyURL(const QUrl&)));
}

void StaticModelRenderer::simulate(float deltaTime) {
    // update the bounds
    Box bounds;
    if (_model->isActive()) {
        const Extents& extents = _model->getGeometry()->getFBXGeometry().meshExtents;
        bounds = Box(extents.minimum, extents.maximum);
    }
    static_cast<StaticModel*>(parent())->setBounds(glm::translate(_model->getTranslation()) *
        glm::mat4_cast(_model->getRotation()) * glm::scale(_model->getScale()) * bounds);
    _model->simulate(deltaTime);
}

void StaticModelRenderer::renderUnclipped(float alpha, Mode mode) {
    switch (mode) {
        case DIFFUSE_MODE:
            _model->render(alpha, Model::DIFFUSE_RENDER_MODE);
            break;
            
        case NORMAL_MODE:
            _model->render(alpha, Model::NORMAL_RENDER_MODE);
            break;
            
        default:
            _model->render(alpha);
            break;
    }
    _model->render(alpha);
}

bool StaticModelRenderer::findRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
        const glm::vec3& clipMinimum, float clipSize, float& distance) const {
    return _model->findRayIntersection(origin, direction, distance);
}

void StaticModelRenderer::applyTranslation(const glm::vec3& translation) {
    _model->setTranslation(translation);
}

void StaticModelRenderer::applyRotation(const glm::quat& rotation) {
    _model->setRotation(rotation);
}

void StaticModelRenderer::applyScale(float scale) {
    const float SCALE_MULTIPLIER = 0.0006f;
    _model->setScale(glm::vec3(scale, scale, scale) * SCALE_MULTIPLIER);
}

void StaticModelRenderer::applyURL(const QUrl& url) {
    _model->setURL(url);
}
