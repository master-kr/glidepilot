
#include "selfdrive/ui/qt/onroad/annotated_camera.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

#include "common/swaglog.h"
#include "selfdrive/ui/qt/onroad/buttons.h"
#include "selfdrive/ui/qt/util.h"

// Window that shows camera view and variety of info drawn on top
AnnotatedCameraWidget::AnnotatedCameraWidget(VisionStreamType type, QWidget* parent) : last_update_params(0), fps_filter(UI_FREQ, 3, 1. / UI_FREQ), CameraWidget("camerad", type, true, parent) {
  pm = std::make_unique<PubMaster, const std::initializer_list<const char *>>({"uiDebug"});

  main_layout = new QVBoxLayout(this);
  main_layout->setMargin(UI_BORDER_SIZE);
  main_layout->setSpacing(0);

  experimental_btn = new ExperimentalButton(this);
  main_layout->addWidget(experimental_btn, 0, Qt::AlignTop | Qt::AlignRight);

  dm_img = loadPixmap("../assets/img_driver_face.png", {img_size + 5, img_size + 5});

  // neokii
  ic_brake = QPixmap("../assets/images/img_brake_disc.png").scaled(img_size, img_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  ic_autohold_warning = QPixmap("../assets/images/img_autohold_warning.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  ic_autohold_active = QPixmap("../assets/images/img_autohold_active.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  ic_nda = QPixmap("../assets/images/img_nda.png");
  ic_hda = QPixmap("../assets/images/img_hda.png");
  ic_nda2 = QPixmap("../assets/images/img_nda2.png");
  ic_hda2 = QPixmap("../assets/images/img_hda2.png");
  ic_tire_pressure = QPixmap("../assets/images/img_tire_pressure.png");
  ic_turn_signal_l = QPixmap("../assets/images/turn_signal_l.png");
  ic_turn_signal_r = QPixmap("../assets/images/turn_signal_r.png");
  ic_satellite = QPixmap("../assets/images/satellite.png");
  ic_safety_speed_bump = QPixmap("../assets/images/safety_speed_bump.png");

  const int size = 150;
  ic_ts_green[0] = QPixmap("../assets/images/ts/green_off.svg").scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  ic_ts_green[1] = QPixmap("../assets/images/ts/green_on.svg").scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  ic_ts_left[0] = QPixmap("../assets/images/ts/left_off.svg").scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  ic_ts_left[1] = QPixmap("../assets/images/ts/left_on.svg").scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  ic_ts_red[0] = QPixmap("../assets/images/ts/red_off.svg").scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  ic_ts_red[1] = QPixmap("../assets/images/ts/red_on.svg").scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

  // screen recoder - neokii

  record_timer = std::make_shared<QTimer>();
	QObject::connect(record_timer.get(), &QTimer::timeout, [=]() {
    if(recorder) {
      recorder->update_screen();
    }
  });
	record_timer->start(1000/UI_FREQ);

	recorder = new ScreenRecoder(this);
	main_layout->addWidget(recorder, 0, Qt::AlignBottom | Qt::AlignRight);
}

void AnnotatedCameraWidget::initializeGL() {
  CameraWidget::initializeGL();
  qInfo() << "OpenGL version:" << QString((const char*)glGetString(GL_VERSION));
  qInfo() << "OpenGL vendor:" << QString((const char*)glGetString(GL_VENDOR));
  qInfo() << "OpenGL renderer:" << QString((const char*)glGetString(GL_RENDERER));
  qInfo() << "OpenGL language version:" << QString((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));

  prev_draw_t = millis_since_boot();
  setBackgroundColor(bg_colors[STATUS_DISENGAGED]);
}

void AnnotatedCameraWidget::updateState(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  experimental_btn->updateState(s);

  const auto cs = sm["controlsState"].getControlsState();

  // update DM icons
  auto dm_state = sm["driverMonitoringState"].getDriverMonitoringState();
  dmActive = dm_state.getIsActiveMode();
  rightHandDM = dm_state.getIsRHD();

  hideBottomIcons = (cs.getAlertSize() != cereal::ControlsState::AlertSize::NONE);
  dm_fade_state = std::clamp(dm_fade_state+0.2*(0.5-dmActive), 0.0, 1.0);
}

void AnnotatedCameraWidget::updateFrameMat() {
  CameraWidget::updateFrameMat();
  UIState *s = uiState();
  int w = width(), h = height();

  s->fb_w = w;
  s->fb_h = h;

  // Apply transformation such that video pixel coordinates match video
  // 1) Put (0, 0) in the middle of the video
  // 2) Apply same scaling as video
  // 3) Put (0, 0) in top left corner of video
  s->car_space_transform.reset();
  s->car_space_transform.translate(w / 2 - x_offset, h / 2 - y_offset)
      .scale(zoom, zoom)
      .translate(-intrinsic_matrix.v[2], -intrinsic_matrix.v[5]);
}

void AnnotatedCameraWidget::drawLaneLines(QPainter &painter, const UIState *s) {
  painter.save();

  const UIScene &scene = s->scene;
  SubMaster &sm = *(s->sm);

  // lanelines
  for (int i = 0; i < std::size(scene.lane_line_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, std::clamp<float>(scene.lane_line_probs[i], 0.0, 0.7)));
    painter.drawPolygon(scene.lane_line_vertices[i]);
  }

  // road edges
  for (int i = 0; i < std::size(scene.road_edge_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - scene.road_edge_stds[i], 0.0, 1.0)));
    painter.drawPolygon(scene.road_edge_vertices[i]);
  }

  // paint path
  QLinearGradient bg(0, height(), 0, 0);
  if (sm["controlsState"].getControlsState().getExperimentalMode()) {
    // The first half of track_vertices are the points for the right side of the path
    const auto &acceleration = sm["modelV2"].getModelV2().getAcceleration().getX();
    const int max_len = std::min<int>(scene.track_vertices.length() / 2, acceleration.size());

    for (int i = 0; i < max_len; ++i) {
      // Some points are out of frame
      int track_idx = max_len - i - 1;  // flip idx to start from bottom right
      if (scene.track_vertices[track_idx].y() < 0 || scene.track_vertices[track_idx].y() > height()) continue;

      // Flip so 0 is bottom of frame
      float lin_grad_point = (height() - scene.track_vertices[track_idx].y()) / height();

      // speed up: 120, slow down: 0
      float path_hue = fmax(fmin(60 + acceleration[i] * 35, 120), 0);
      // FIXME: painter.drawPolygon can be slow if hue is not rounded
      path_hue = int(path_hue * 100 + 0.5) / 100;

      float saturation = fmin(fabs(acceleration[i] * 1.5), 1);
      float lightness = util::map_val(saturation, 0.0f, 1.0f, 0.95f, 0.62f);  // lighter when grey
      float alpha = util::map_val(lin_grad_point, 0.75f / 2.f, 0.75f, 0.4f, 0.0f);  // matches previous alpha fade
      bg.setColorAt(lin_grad_point, QColor::fromHslF(path_hue / 360., saturation, lightness, alpha));

      // Skip a point, unless next is last
      i += (i + 2) < max_len ? 1 : 0;
    }

  } else {
    bg.setColorAt(0.0, QColor::fromHslF(148 / 360., 0.94, 0.51, 0.4));
    bg.setColorAt(0.5, QColor::fromHslF(112 / 360., 1.0, 0.68, 0.35));
    bg.setColorAt(1.0, QColor::fromHslF(112 / 360., 1.0, 0.68, 0.0));
  }
  painter.setBrush(bg);
  painter.drawPolygon(scene.track_vertices);

  painter.restore();
}

void AnnotatedCameraWidget::drawLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data, const QPointF &vd, bool is_radar) {
  painter.save();
  const float speedBuff = 10.;
  const float leadBuff = 40.;
  const float d_rel = lead_data.getDRel();
  const float v_rel = lead_data.getVRel();

  float fillAlpha = 0;
  if (d_rel < leadBuff) {
    fillAlpha = 255 * (1.0 - (d_rel / leadBuff));
    if (v_rel < 0) {
      fillAlpha += 255 * (-1 * (v_rel / speedBuff));
    }
    fillAlpha = (int)(fmin(fillAlpha, 255));
  }

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  float x = std::clamp((float)vd.x(), 0.f, width() - sz / 2);
  float y = std::fmin(height() - sz * .6, (float)vd.y());

  float g_xo = sz / 5;
  float g_yo = sz / 10;

  QPointF glow[] = {{x + (sz * 1.35) + g_xo, y + sz + g_yo}, {x, y - g_yo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo}};
  painter.setBrush(is_radar ? QColor(86, 121, 216, 255) : QColor(218, 202, 37, 255));
  painter.drawPolygon(glow, std::size(glow));

  // chevron
  QPointF chevron[] = {{x + (sz * 1.25), y + sz}, {x, y}, {x - (sz * 1.25), y + sz}};
  painter.setBrush(redColor(fillAlpha));
  painter.drawPolygon(chevron, std::size(chevron));

  painter.restore();
}

void AnnotatedCameraWidget::paintGL() {
}

void AnnotatedCameraWidget::paintEvent(QPaintEvent *event) {
  UIState *s = uiState();
  SubMaster &sm = *(s->sm);
  const double start_draw_t = millis_since_boot();
  const cereal::ModelDataV2::Reader &model = sm["modelV2"].getModelV2();

  QPainter p(this);

  // Wide or narrow cam dependent on speed
  bool has_wide_cam = available_streams.count(VISION_STREAM_WIDE_ROAD);
  if (has_wide_cam) {
    float v_ego = sm["carState"].getCarState().getVEgo();
    if ((v_ego < 10) || available_streams.size() == 1) {
      wide_cam_requested = true;
    } else if (v_ego > 15) {
      wide_cam_requested = false;
    }
    wide_cam_requested = wide_cam_requested && sm["controlsState"].getControlsState().getExperimentalMode();
    // for replay of old routes, never go to widecam
    wide_cam_requested = wide_cam_requested && s->scene.calibration_wide_valid;
  }

  if(s->scene.show_driver_camera) {
    CameraWidget::setStreamType(VISION_STREAM_DRIVER);
  }
  else {
    CameraWidget::setStreamType(wide_cam_requested ? VISION_STREAM_WIDE_ROAD : VISION_STREAM_ROAD);
  }

  s->scene.wide_cam = CameraWidget::getStreamType() == VISION_STREAM_WIDE_ROAD;
  if (s->scene.calibration_valid) {
    auto calib = s->scene.wide_cam ? s->scene.view_from_wide_calib : s->scene.view_from_calib;
    CameraWidget::updateCalibration(calib);
  } else {
    CameraWidget::updateCalibration(DEFAULT_CALIBRATION);
  }

  p.beginNativePainting();
  CameraWidget::setFrameId(model.getFrameId());
  CameraWidget::paintGL();
  p.endNativePainting();

  if (s->scene.world_objects_visible && !s->scene.show_driver_camera) {
    update_model(s, model);
    // DMoji
    if (!hideBottomIcons && (sm.rcv_frame("driverStateV2") > s->scene.started_frame)) {
      update_dmonitoring(s, sm["driverStateV2"].getDriverStateV2(), dm_fade_state, false);
      drawDriverState(p, s);
    }
  }

  if(!s->scene.show_driver_camera) {
    drawHud(p, model);
  }

  double cur_draw_t = millis_since_boot();
  double dt = cur_draw_t - prev_draw_t;
  double fps = fps_filter.update(1. / dt * 1000);
  if (fps < 15) {
    LOGW("slow frame rate: %.2f fps", fps);
  }
  prev_draw_t = cur_draw_t;

  // publish debug msg
  MessageBuilder msg;
  auto m = msg.initEvent().initUiDebug();
  m.setDrawTimeMillis(cur_draw_t - start_draw_t);
  pm->send("uiDebug", msg);
}

void AnnotatedCameraWidget::showEvent(QShowEvent *event) {
  CameraWidget::showEvent(event);

  auto now = millis_since_boot();
  if(now - last_update_params > 1000*5) {
    last_update_params = now;
    ui_update_params(uiState());
  }

  prev_draw_t = millis_since_boot();
}

void AnnotatedCameraWidget::drawText(QPainter &p, int x, int y, const QString &text, int alpha) {
  QRect real_rect = p.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void AnnotatedCameraWidget::drawTextWithColor(QPainter &p, int x, int y, const QString &text, QColor& color) {
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(color);
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void AnnotatedCameraWidget::drawRoundedText(QPainter &p, int x, int y, const QString &text, QColor& color, QColor& bgColor, int cornerRadius) {
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});
  real_rect.adjust(-10, 0, 10, 0);

  p.setBrush(QBrush(bgColor));
  p.setPen(Qt::NoPen);
  p.drawRoundedRect(real_rect, cornerRadius, cornerRadius);

  p.setPen(color);
  p.drawText(real_rect, Qt::AlignCenter, text);
}

void AnnotatedCameraWidget::drawText2(QPainter &p, int x, int y, int flags, const QString &text, const QColor& color) {
  QFontMetrics fm(p.font());
  QRect rect = fm.boundingRect(text);
  rect.adjust(-1, -1, 1, 1);
  p.setPen(color);
  p.drawText(QRect(x, y, rect.width()+1, rect.height()), flags, text);
}

void AnnotatedCameraWidget::drawHud(QPainter &p, const cereal::ModelDataV2::Reader &model) {

  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setOpacity(1.);

  // Header gradient
  QLinearGradient bg(0, UI_HEADER_HEIGHT - (UI_HEADER_HEIGHT / 2.5), 0, UI_HEADER_HEIGHT);
  bg.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.45));
  bg.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0));
  p.fillRect(0, 0, width(), UI_HEADER_HEIGHT, bg);

  UIState *s = uiState();

  const SubMaster &sm = *(s->sm);
  drawLaneLines(p, s);

  auto radar_state = sm["radarState"].getRadarState();
  update_leads(s, radar_state, model.getPosition());
  auto lead_one = radar_state.getLeadOne();
  auto lead_two = radar_state.getLeadTwo();
  if (lead_two.getStatus()) {
    drawLead(p, lead_two, s->scene.lead_vertices[1], s->scene.lead_radar[1]);
  }
  if (lead_one.getStatus()) {
    drawLead(p, lead_one, s->scene.lead_vertices[0], s->scene.lead_radar[0]);
  }

  drawMaxSpeed(p);
  drawSpeed(p);
  drawSteer(p);
  drawDeviceState(p);
  //drawTurnSignals(p);
  drawGpsStatus(p);

  if(!drawTrafficSignal(p))
    drawMisc(p);

  drawDebugText(p);

  const auto controls_state = sm["controlsState"].getControlsState();
  const auto car_params = sm["carParams"].getCarParams();
  const auto live_params = sm["liveParameters"].getLiveParameters();
  const auto car_control = sm["carControl"].getCarControl();
  const auto live_torque_params = sm["liveTorqueParameters"].getLiveTorqueParameters();
  const auto torque_state = controls_state.getLateralControlState().getTorqueState();
  const auto car_state = sm["carState"].getCarState();
  const auto ex_state = car_state.getExState();

  QString infoText;
  infoText.sprintf("TP(%.2f/%.2f) LTP(%.2f/%.2f/%.0f) AO(%.2f/%.2f) SR(%.2f) SAD(%.2f) LAD(%.2f) SCC(%d)",

                      torque_state.getLatAccelFactor(),
                      torque_state.getFriction(),

                      live_torque_params.getLatAccelFactorRaw(),
                      live_torque_params.getFrictionCoefficientRaw(),
                      live_torque_params.getTotalBucketPoints(),

                      live_params.getAngleOffsetDeg(),
                      live_params.getAngleOffsetAverageDeg(),

                      car_control.getSteerRatio(),
                      ex_state.getSteerActuatorDelay(),
                      ex_state.getLongActuatorDelay(),

                      car_params.getSccBus()
                      );

  // info

  p.save();
  p.setFont(InterFont(34, QFont::Normal));
  p.setPen(QColor(0xff, 0xff, 0xff, 200));
  p.drawText(rect().left() + 20, rect().height() - 15, infoText);
  p.restore();

  drawBottomIcons(p);
}

void AnnotatedCameraWidget::drawSpeed(QPainter &p) {
  p.save();
  UIState *s = uiState();
  const SubMaster &sm = *(s->sm);
  float cur_speed = std::max(0.0, sm["carState"].getCarState().getVEgoCluster() * (s->scene.is_metric ? MS_TO_KPH : MS_TO_MPH));
  auto car_state = sm["carState"].getCarState();
  float accel = car_state.getAEgo();

  QColor color = QColor(255, 255, 255, 230);

  if(accel > 0) {
    int a = (int)(255.f - (180.f * (accel/2.f)));
    a = std::min(a, 255);
    a = std::max(a, 80);
    color = QColor(a, a, 255, 230);
  }
  else {
    int a = (int)(255.f - (255.f * (-accel/3.f)));
    a = std::min(a, 255);
    a = std::max(a, 60);
    color = QColor(255, a, a, 230);
  }

  QString speed;
  speed.sprintf("%.0f", cur_speed);
  p.setFont(InterFont(176, QFont::Bold));
  drawTextWithColor(p, rect().center().x(), 230, speed, color);

  p.setFont(InterFont(66, QFont::Normal));
  drawText(p, rect().center().x(), 310, s->scene.is_metric ? "km/h" : "mph", 200);

  p.restore();
}

QRect getRect(QPainter &p, int flags, QString text) {
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  return fm.boundingRect(init_rect, flags, text);
}

void AnnotatedCameraWidget::drawMaxSpeed(QPainter &p) {
  p.save();

  UIState *s = uiState();
  const SubMaster &sm = *(s->sm);
  //const auto car_control = sm["carControl"].getCarControl();
  const auto car_state = sm["carState"].getCarState();
  const auto ex_state = car_state.getExState();
  //const auto car_params = sm["carParams"].getCarParams();
  const auto navi_data = sm["naviData"].getNaviData();

  const auto cruiseState = car_state.getCruiseState();

  bool is_metric = s->scene.is_metric;
  //int scc_bus = car_params.getSccBus();

  // kph
  float applyMaxSpeed = ex_state.getApplyMaxSpeed();
  float cruiseMaxSpeed = ex_state.getCruiseMaxSpeed();

  bool is_cruise_set = cruiseState.getEnabled();

  int activeNDA = navi_data.getActive();
  int roadLimitSpeed = navi_data.getRoadLimitSpeed();
  int camLimitSpeed = navi_data.getCamLimitSpeed();
  int camLimitSpeedLeftDist = navi_data.getCamLimitSpeedLeftDist();
  int sectionLimitSpeed = navi_data.getSectionLimitSpeed();
  int sectionLeftDist = navi_data.getSectionLeftDist();
  int isNda2 = navi_data.getIsNda2();

  int limit_speed = 0;
  int left_dist = 0;

  if(camLimitSpeed > 0 && camLimitSpeedLeftDist > 0) {
    limit_speed = camLimitSpeed;
    left_dist = camLimitSpeedLeftDist;
  }
  else if(sectionLimitSpeed > 0 && sectionLeftDist > 0) {
    limit_speed = sectionLimitSpeed;
    left_dist = sectionLeftDist;
  }

  if(activeNDA > 0) {
      p.setOpacity(1.f);
      if(isNda2) {
        int w = 155;
        int h = 54;
        int x = (width() + (UI_BORDER_SIZE*2))/2 - w/2 - UI_BORDER_SIZE;
        int y = 40 - UI_BORDER_SIZE;
        p.drawPixmap(x, y, w, h, activeNDA == 1 ? ic_nda2 : ic_hda2);
      }
      else {
        int w = 120;
        int h = 54;
        int x = (width() + (UI_BORDER_SIZE*2))/2 - w/2 - UI_BORDER_SIZE;
        int y = 40 - UI_BORDER_SIZE;
        p.drawPixmap(x, y, w, h, activeNDA == 1 ? ic_nda : ic_hda);
      }
  }
  else {
    limit_speed = ex_state.getNavSpeedLimit();
  }


  const int x_start = 30;
  const int y_start = 30;

  int board_width = 210;
  int board_height = 384;

  const int corner_radius = 32;
  int max_speed_height = 210;

  QColor bgColor = QColor(0, 0, 0, 166);

  {
    // draw board
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);

    if(limit_speed > 0) {
      board_width = limit_speed < 100 ? 210 : 230;
      board_height = max_speed_height + board_width;

      path.addRoundedRect(QRectF(x_start, y_start, board_width, board_height-board_width/2), corner_radius, corner_radius);
      path.addRoundedRect(QRectF(x_start, y_start+corner_radius, board_width, board_height-corner_radius), board_width/2, board_width/2);
    }
    else if(roadLimitSpeed > 0 && roadLimitSpeed < 200) {
      board_height = 485;
      path.addRoundedRect(QRectF(x_start, y_start, board_width, board_height), corner_radius, corner_radius);
    }
    else {
      max_speed_height = 235;
      board_height = max_speed_height;
      path.addRoundedRect(QRectF(x_start, y_start, board_width, board_height), corner_radius, corner_radius);
    }

    p.setPen(Qt::NoPen);
    p.fillPath(path.simplified(), bgColor);
  }

  QString str;

  // Max Speed
  {
    p.setPen(QColor(255, 255, 255, 230));

    if(is_cruise_set) {
      p.setFont(InterFont(80, QFont::Bold));

      if(is_metric)
        str.sprintf( "%d", (int)(cruiseMaxSpeed + 0.5));
      else
        str.sprintf( "%d", (int)(cruiseMaxSpeed*KM_TO_MILE + 0.5));
    }
    else {
      p.setFont(InterFont(60, QFont::Bold));
      str = "N/A";
    }

    QRect speed_rect = getRect(p, Qt::AlignCenter, str);
    QRect max_speed_rect(x_start, y_start, board_width, max_speed_height/2);
    speed_rect.moveCenter({max_speed_rect.center().x(), 0});
    speed_rect.moveTop(max_speed_rect.top() + 35);
    p.drawText(speed_rect, Qt::AlignCenter | Qt::AlignVCenter, str);
  }


  // applyMaxSpeed
  {
    p.setPen(QColor(255, 255, 255, 180));

    p.setFont(InterFont(50, QFont::Bold));
    if(is_cruise_set && applyMaxSpeed > 0) {
      if(is_metric)
        str.sprintf( "%d", (int)(applyMaxSpeed + 0.5));
      else
        str.sprintf( "%d", (int)(applyMaxSpeed*KM_TO_MILE + 0.5));
    }
    else {
      str = "MAX";
    }

    QRect speed_rect = getRect(p, Qt::AlignCenter, str);
    QRect max_speed_rect(x_start, y_start + max_speed_height/2, board_width, max_speed_height/2);
    speed_rect.moveCenter({max_speed_rect.center().x(), 0});
    speed_rect.moveTop(max_speed_rect.top() + 24);
    p.drawText(speed_rect, Qt::AlignCenter | Qt::AlignVCenter, str);
  }

  //
  if(limit_speed > 0) {
    QRect board_rect = QRect(x_start, y_start+board_height-board_width, board_width, board_width);

    if(navi_data.getCamType() == 22) {
      int padding = 25;
      board_rect.adjust(padding, padding, -padding, -padding);
      p.drawPixmap(board_rect.x(), board_rect.y()-10, board_rect.width(), board_rect.height(), ic_safety_speed_bump);
    }
    else {
      int padding = 14;
      board_rect.adjust(padding, padding, -padding, -padding);
      p.setBrush(QBrush(Qt::white));
      p.drawEllipse(board_rect);

      padding = 18;
      board_rect.adjust(padding, padding, -padding, -padding);

      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(Qt::red, 25));
      p.drawEllipse(board_rect);

      p.setPen(QPen(Qt::black, padding));

      str.sprintf("%d", limit_speed);
      p.setFont(InterFont(70, QFont::Bold));

      QRect text_rect = getRect(p, Qt::AlignCenter, str);
      QRect b_rect = board_rect;
      text_rect.moveCenter({b_rect.center().x(), 0});
      text_rect.moveTop(b_rect.top() + (b_rect.height() - text_rect.height()) / 2);
      p.drawText(text_rect, Qt::AlignCenter, str);
    }

    if(left_dist > 0) {
      // left dist
      QRect rcLeftDist;
      QString strLeftDist;

      if(left_dist < 1000)
        strLeftDist.sprintf("%dm", left_dist);
      else
        strLeftDist.sprintf("%.1fkm", left_dist / 1000.f);

      QFont font("Inter");
      font.setPixelSize(55);
      font.setStyleName("Bold");

      QFontMetrics fm(font);
      int width = fm.width(strLeftDist);

      int padding = 10;

      int center_x = x_start + board_width / 2;
      rcLeftDist.setRect(center_x - width / 2, y_start+board_height+15, width, font.pixelSize()+10);
      rcLeftDist.adjust(-padding*2, -padding, padding*2, padding);

      p.setPen(Qt::NoPen);
      p.setBrush(bgColor);
      p.drawRoundedRect(rcLeftDist, 20, 20);

      p.setFont(InterFont(55, QFont::Bold));
      p.setBrush(Qt::NoBrush);
      p.setPen(QColor(255, 255, 255, 230));
      p.drawText(rcLeftDist, Qt::AlignCenter|Qt::AlignVCenter, strLeftDist);
    }
  }
  else if(roadLimitSpeed > 0 && roadLimitSpeed < 200) {
    QRectF board_rect = QRectF(x_start, y_start+max_speed_height, board_width, board_height-max_speed_height);
    int padding = 14;
    board_rect.adjust(padding, padding, -padding, -padding);
    p.setBrush(QBrush(Qt::white));
    p.drawRoundedRect(board_rect, corner_radius-padding/2, corner_radius-padding/2);

    padding = 10;
    board_rect.adjust(padding, padding, -padding, -padding);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, padding));
    p.drawRoundedRect(board_rect, corner_radius-12, corner_radius-12);

    {
      str = "SPEED\nLIMIT";
      p.setFont(InterFont(35, QFont::Bold));

      QRect text_rect = getRect(p, Qt::AlignCenter, str);
      QRect b_rect(board_rect.x(), board_rect.y(), board_rect.width(), board_rect.height()/2);
      text_rect.moveCenter({b_rect.center().x(), 0});
      text_rect.moveTop(b_rect.top() + 20);
      p.drawText(text_rect, Qt::AlignCenter, str);
    }

    {
      str.sprintf("%d", roadLimitSpeed);
      p.setFont(InterFont(75, QFont::Bold));

      QRect text_rect = getRect(p, Qt::AlignCenter, str);
      QRect b_rect(board_rect.x(), board_rect.y()+board_rect.height()/2, board_rect.width(), board_rect.height()/2);
      text_rect.moveCenter({b_rect.center().x(), 0});
      text_rect.moveTop(b_rect.top() + 3);
      p.drawText(text_rect, Qt::AlignCenter, str);
    }

    {
      p.setFont(InterFont(10, QFont::Bold));

      QRect text_rect = getRect(p, Qt::AlignCenter, str);
      QRect b_rect(board_rect.x(), board_rect.y(), board_rect.width(), board_rect.height()/2);
      text_rect.moveCenter({b_rect.center().x(), 0});
      text_rect.moveTop(b_rect.top() + 20);
      p.drawText(text_rect, Qt::AlignCenter, str);
    }
  }

  p.restore();
}

void AnnotatedCameraWidget::drawSteer(QPainter &p) {
  p.save();

  int x = 30;
  int y = 540;

  const SubMaster &sm = *(uiState()->sm);
  auto car_state = sm["carState"].getCarState();
  //const auto ex_state = car_state.getExState();
  auto car_control = sm["carControl"].getCarControl();
  auto radar_state = sm["radarState"].getRadarState();
  auto lead_one = radar_state.getLeadOne();
  auto lead_two = radar_state.getLeadTwo();

  float steer_angle = car_state.getSteeringAngleDeg();
  float desire_angle = car_control.getActuators().getSteeringAngleDeg();

  p.setFont(InterFont(50, QFont::Bold));

  QString str;
  int width = 192;

  str.sprintf("%.1f°", steer_angle);
  QRect rect = QRect(x, y, width, width);

  p.setPen(QColor(255, 255, 255, 200));
  p.drawText(rect, Qt::AlignCenter, str);

  str.sprintf("%.1f°", desire_angle);
  rect.setRect(x, y + 80, width, width);

  p.setPen(QColor(155, 255, 155, 200));
  p.drawText(rect, Qt::AlignCenter, str);

  if(lead_one.getStatus()) {
    str.sprintf("%.1fm", lead_one.getDRel());
    rect.setRect(x + 150, y, width, width);
    p.setPen(QColor(255, 255, 255, 200));
    p.drawText(rect, Qt::AlignCenter, str);
  }

  if(lead_two.getStatus()) {
    str.sprintf("%.1fm", lead_two.getDRel());
    rect.setRect(x + 150, y + 80, width, width);
    p.setPen(QColor(255, 255, 255, 200));
    p.drawText(rect, Qt::AlignCenter, str);
  }

  p.restore();
}

template <class T>
float interp(float x, std::initializer_list<T> x_list, std::initializer_list<T> y_list, bool extrapolate)
{
  std::vector<T> xData(x_list);
  std::vector<T> yData(y_list);
  int size = xData.size();

  int i = 0;
  if(x >= xData[size - 2]) {
    i = size - 2;
  }
  else {
    while ( x > xData[i+1] ) i++;
  }
  T xL = xData[i], yL = yData[i], xR = xData[i+1], yR = yData[i+1];
  if (!extrapolate) {
    if ( x < xL ) yR = yL;
    if ( x > xR ) yL = yR;
  }

  T dydx = ( yR - yL ) / ( xR - xL );
  return yL + dydx * ( x - xL );
}

void AnnotatedCameraWidget::drawDeviceState(QPainter &p) {
  p.save();

  const SubMaster &sm = *(uiState()->sm);
  auto deviceState = sm["deviceState"].getDeviceState();

  const auto freeSpacePercent = deviceState.getFreeSpacePercent();

  const auto cpuTempC = deviceState.getCpuTempC();

  float cpuTemp = 0.f;

  if(std::size(cpuTempC) > 0) {
    for(int i = 0; i < std::size(cpuTempC); i++) {
      cpuTemp += cpuTempC[i];
    }
    cpuTemp = cpuTemp / (float)std::size(cpuTempC);
  }

  int w = 192;
  int x = width() - (30 + w) + 8;
  int y = 340 + 80;

  QString str;
  QRect rect;

  p.setFont(InterFont(50, QFont::Bold));
  str.sprintf("%.0f%%", freeSpacePercent);
  rect = QRect(x, y, w, w);

  int r = interp<float>(freeSpacePercent, {10.f, 90.f}, {255.f, 200.f}, false);
  int g = interp<float>(freeSpacePercent, {10.f, 90.f}, {200.f, 255.f}, false);
  p.setPen(QColor(r, g, 200, 200));
  p.drawText(rect, Qt::AlignCenter, str);

  y += 55;
  p.setFont(InterFont(25, QFont::Bold));
  rect = QRect(x, y, w, w);
  p.setPen(QColor(255, 255, 255, 200));
  p.drawText(rect, Qt::AlignCenter, "STORAGE");

  y += 80;
  p.setFont(InterFont(50, QFont::Bold));
  str.sprintf("%.0f°C", cpuTemp);
  rect = QRect(x, y, w, w);
  r = interp<float>(cpuTemp, {50.f, 90.f}, {200.f, 255.f}, false);
  g = interp<float>(cpuTemp, {50.f, 90.f}, {255.f, 200.f}, false);
  p.setPen(QColor(r, g, 200, 200));
  p.drawText(rect, Qt::AlignCenter, str);

  y += 55;
  p.setFont(InterFont(25, QFont::Bold));
  rect = QRect(x, y, w, w);
  p.setPen(QColor(255, 255, 255, 200));
  p.drawText(rect, Qt::AlignCenter, "CPU");

  p.restore();
}

void AnnotatedCameraWidget::drawTurnSignals(QPainter &p) {
  p.save();

  static int blink_index = 0;
  static int blink_wait = 0;
  static double prev_ts = 0.0;

  if(blink_wait > 0) {
    blink_wait--;
    blink_index = 0;
  }
  else {
    const SubMaster &sm = *(uiState()->sm);
    auto car_state = sm["carState"].getCarState();
    //const auto ex_state = car_state.getExState();
    bool left_on = car_state.getLeftBlinker();
    bool right_on = car_state.getRightBlinker();

    const float img_alpha = 0.8f;
    const int fb_w = width() / 2 - 200;
    const int center_x = width() / 2;
    const int w = fb_w / 25;
    const int h = 160;
    const int gap = fb_w / 25;
    const int margin = (int)(fb_w / 3.8f);
    const int base_y = (height() - h) / 2;
    const int draw_count = 8;

    int x = center_x;
    int y = base_y;

    if(left_on) {
      for(int i = 0; i < draw_count; i++) {
        float alpha = img_alpha;
        int d = std::abs(blink_index - i);
        if(d > 0)
          alpha /= d*2;

        p.setOpacity(alpha);
        float factor = (float)draw_count / (i + draw_count);
        p.drawPixmap(x - w - margin, y + (h-h*factor)/2, w*factor, h*factor, ic_turn_signal_l);
        x -= gap + w;
      }
    }

    x = center_x;
    if(right_on) {
      for(int i = 0; i < draw_count; i++) {
        float alpha = img_alpha;
        int d = std::abs(blink_index - i);
        if(d > 0)
          alpha /= d*2;

        float factor = (float)draw_count / (i + draw_count);
        p.setOpacity(alpha);
        p.drawPixmap(x + margin, y + (h-h*factor)/2, w*factor, h*factor, ic_turn_signal_r);
        x += gap + w;
      }
    }

    if(left_on || right_on) {

      double now = millis_since_boot();
      if(now - prev_ts > 900/UI_FREQ) {
        prev_ts = now;
        blink_index++;
      }

      if(blink_index >= draw_count) {
        blink_index = draw_count - 1;
        blink_wait = UI_FREQ/4;
      }
    }
    else {
      blink_index = 0;
    }
  }

  p.restore();
}

void AnnotatedCameraWidget::drawGpsStatus(QPainter &p) {
  const SubMaster &sm = *(uiState()->sm);
  auto gps = sm["gpsLocationExternal"].getGpsLocationExternal();
  float accuracy = gps.getHorizontalAccuracy();
  if(accuracy < 0.01f || accuracy > 20.f)
    return;

  int w = 90;
  int h = 75;
  int x = width() - w - 75 + 8;
  int y = 240;

  p.save();

  p.setOpacity(0.8);
  p.drawPixmap(x, y, w, h, ic_satellite);

  p.setFont(InterFont(40, QFont::Bold));
  p.setPen(QColor(255, 255, 255, 200));
  p.setRenderHint(QPainter::TextAntialiasing);

  QRect rect = QRect(x, y + h + 10, w, 40);
  rect.adjust(-30, 0, 30, 0);

  QString str;
  str.sprintf("%.1fm", accuracy);
  p.drawText(rect, Qt::AlignHCenter, str);

  p.restore();
}

void AnnotatedCameraWidget::drawDebugText(QPainter &p) {

  /*p.save();

  const SubMaster &sm = *(uiState()->sm);
  QString str, temp;

  int y = 80;

  const int text_x = width()/2 + 220;
  auto car_control = sm["carControl"].getCarControl();

  p.setFont(InterFont(40, QFont::Normal));
  p.setPen(QColor(255, 255, 255, 200));

  QRect rect = QRect(text_x, y, width()/2 - 120, height() - y);

  p.drawText(rect, Qt::AlignLeft, QString::fromStdString(car_control.getDebugText().cStr()));

  p.restore();*/
}

void AnnotatedCameraWidget::drawDriverState(QPainter &painter, const UIState *s) {
  const UIScene &scene = s->scene;

  painter.save();

  // base icon
  int x = radius / 2 + (UI_BORDER_SIZE * 2); //+ (radius + 50);
  int y = rect().bottom() - UI_FOOTER_HEIGHT / 2 - 10;

  float opacity = dmActive ? 0.65f : 0.15f;
  drawIcon(painter, QPoint(x, y), dm_img, blackColor(70), opacity);

  // face
  QPointF face_kpts_draw[std::size(default_face_kpts_3d)];
  float kp;
  for (int i = 0; i < std::size(default_face_kpts_3d); ++i) {
    kp = (scene.face_kpts_draw[i].v[2] - 8) / 120 + 1.0;
    face_kpts_draw[i] = QPointF(scene.face_kpts_draw[i].v[0] * kp + x, scene.face_kpts_draw[i].v[1] * kp + y);
  }

  painter.setPen(QPen(QColor::fromRgbF(1.0, 1.0, 1.0, opacity), 5.2, Qt::SolidLine, Qt::RoundCap));
  painter.drawPolyline(face_kpts_draw, std::size(default_face_kpts_3d));

  // tracking arcs
  const int arc_l = 133;
  const float arc_t_default = 6.7;
  const float arc_t_extend = 12.0;
  QColor arc_color = QColor::fromRgbF(0.545 - 0.445 * s->engaged(),
                                      0.545 + 0.4 * s->engaged(),
                                      0.545 - 0.285 * s->engaged(),
                                      0.4 * (1.0 - dm_fade_state));
  float delta_x = -scene.driver_pose_sins[1] * arc_l / 2;
  float delta_y = -scene.driver_pose_sins[0] * arc_l / 2;
  painter.setPen(QPen(arc_color, arc_t_default+arc_t_extend*fmin(1.0, scene.driver_pose_diff[1] * 5.0), Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(std::fmin(x + delta_x, x), y - arc_l / 2, fabs(delta_x), arc_l), (scene.driver_pose_sins[1]>0 ? 90 : -90) * 16, 180 * 16);
  painter.setPen(QPen(arc_color, arc_t_default+arc_t_extend*fmin(1.0, scene.driver_pose_diff[0] * 5.0), Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(x - arc_l / 2, std::fmin(y + delta_y, y), arc_l, fabs(delta_y)), (scene.driver_pose_sins[0]>0 ? 0 : 180) * 16, 180 * 16);

  painter.restore();
}

static const QColor get_tpms_color(float tpms) {
    if(tpms < 5 || tpms > 60) // N/A
        return QColor(255, 255, 255, 220);
    if(tpms < 31)
        return QColor(255, 90, 90, 220);
    return QColor(255, 255, 255, 220);
}

static const QString get_tpms_text(float tpms) {
    if(tpms < 5 || tpms > 60)
        return "";

    char str[32];
    snprintf(str, sizeof(str), "%.0f", round(tpms));
    return QString(str);
}

void AnnotatedCameraWidget::drawBottomIcons(QPainter &p) {
  p.save();
  const SubMaster &sm = *(uiState()->sm);
  const auto car_state = sm["carState"].getCarState();
  const auto ex_state = car_state.getExState();
  //const auto car_control = sm["carControl"].getCarControl();
  const auto car_params = (*uiState()->sm)["carParams"].getCarParams();
  const auto tpms = ex_state.getTpms();

  int n = 1;

  // tpms
  if(tpms.getEnabled()) {
    const int w = 58;
    const int h = 126;
    const int x = radius / 2 + (UI_BORDER_SIZE * 2) + (radius + 50) * n - w/2;
    const int y = height() - h - 85;

    const float fl = tpms.getFl();
    const float fr = tpms.getFr();
    const float rl = tpms.getRl();
    const float rr = tpms.getRr();

    p.setOpacity(0.8);
    p.drawPixmap(x, y, w, h, ic_tire_pressure);

    p.setFont(InterFont(38, QFont::Bold));
    QFontMetrics fm(p.font());
    QRect rcFont = fm.boundingRect("9");

    int center_x = x + 3;
    int center_y = y + h/2;
    const int marginX = (int)(rcFont.width() * 2.7f);
    const int marginY = (int)((h/2 - rcFont.height()) * 0.7f);

    drawText2(p, center_x-marginX, center_y-marginY-rcFont.height(), Qt::AlignRight, get_tpms_text(fl), get_tpms_color(fl));
    drawText2(p, center_x+marginX, center_y-marginY-rcFont.height(), Qt::AlignLeft, get_tpms_text(fr), get_tpms_color(fr));
    drawText2(p, center_x-marginX, center_y+marginY, Qt::AlignRight, get_tpms_text(rl), get_tpms_color(rl));
    drawText2(p, center_x+marginX, center_y+marginY, Qt::AlignLeft, get_tpms_text(rr), get_tpms_color(rr));

    n++;
  }

  int x = radius / 2 + (UI_BORDER_SIZE * 2) + (radius + 50) * n;
  const int y = rect().bottom() - UI_FOOTER_HEIGHT / 2 - 10;

  // cruise gap
  int gap = car_state.getCruiseState().getLeadDistanceBars();
  int autoTrGap = ex_state.getAutoTrGap();

  p.setPen(Qt::NoPen);
  p.setBrush(QBrush(QColor(0, 0, 0, 255 * .1f)));
  p.drawEllipse(x - radius / 2, y - radius / 2, radius, radius);

  QString str;
  float textSize = 50.f;
  QColor textColor = QColor(255, 255, 255, 200);

  if(gap <= 0) {
    str = "N/A";
  }
  else if(gap == autoTrGap) {
    str = "AUTO";
    textColor = QColor(120, 255, 120, 200);
  }
  else {
    str.sprintf("%d", (int)gap);
    textColor = QColor(120, 255, 120, 200);
    textSize = 70.f;
  }

  p.setFont(InterFont(35, QFont::Bold));
  drawText(p, x, y-20, "GAP", 200);

  p.setFont(InterFont(textSize, QFont::Bold));
  drawTextWithColor(p, x, y+50, str, textColor);
  n++;

  // brake
  x = radius / 2 + (UI_BORDER_SIZE * 2) + (radius + 50) * n;
  bool brake_valid = car_state.getBrakeLights();
  float img_alpha = brake_valid ? 1.0f : 0.15f;
  float bg_alpha = brake_valid ? 0.3f : 0.1f;
  drawIcon(p, QPoint(x, y), ic_brake, QColor(0, 0, 0, (255 * bg_alpha)), img_alpha);
  n++;

  // auto hold
  if(car_params.getExFlags() & 1) {
    int autohold = ex_state.getAutoHold();
    if(autohold >= 0) {
      x = radius / 2 + (UI_BORDER_SIZE * 2) + (radius + 50) * n;
      img_alpha = autohold > 0 ? 1.0f : 0.15f;
      bg_alpha = autohold > 0 ? 0.3f : 0.1f;
      drawIcon(p, QPoint(x, y), autohold > 1 ? ic_autohold_warning : ic_autohold_active,
              QColor(0, 0, 0, (255 * bg_alpha)), img_alpha);
    }
    n++;
  }

  p.restore();
}

void AnnotatedCameraWidget::drawMisc(QPainter &p) {
  if(width() < 1080) return;

  p.save();
  UIState *s = uiState();
  const SubMaster &sm = *(s->sm);

  const auto navi_data = sm["naviData"].getNaviData();
  QString currentRoadName = QString::fromStdString(navi_data.getCurrentRoadName().cStr());

  QColor color = QColor(255, 255, 255, 230);

  p.setFont(InterFont(70, QFont::Normal));
  drawText(p, (width()-(UI_BORDER_SIZE*2))/4 + UI_BORDER_SIZE + 20, 140, currentRoadName, 200);

  p.restore();
}

bool AnnotatedCameraWidget::drawTrafficSignal(QPainter &p) {
  UIState *s = uiState();
  const SubMaster &sm = *(s->sm);
  const auto ts = sm["naviData"].getNaviData().getTs();

  if(ts.getDistance() > 0) {
    p.save();

    bool narrow = width() < 1080;

    int ic_size = narrow ? 110 : 130;
    int ic_gap = narrow ? 20 : 30;
    int text_h_margin = 80;
    int center_x = narrow ? ((width()-(UI_BORDER_SIZE*2))/2 + UI_BORDER_SIZE) : ((width()-(UI_BORDER_SIZE*2))/4 + UI_BORDER_SIZE + 40);
    int y = narrow ? 380 : 100;
    int text_size = narrow ? 60 : 70;

    // border
    int border_margin = 10;
    int border_x = center_x - ic_gap - ic_size - ic_size/2 - border_margin;
    QRect border_rect(border_x, y - border_margin, ic_gap*2 + ic_size*3 + border_margin*2,
                      ic_size + border_margin*2);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor backgroundColor(0x00, 0x00, 0x00, 0xCC);
    p.setBrush(QBrush(backgroundColor));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(border_rect, ic_size/2 + border_margin, ic_size/2 + border_margin);

    // signal
    p.drawPixmap(center_x-ic_size/2 - ic_size - ic_gap, y, ic_size, ic_size, ts.getIsRedLightOn() ? ic_ts_red[1] : ic_ts_red[0]);
    p.drawPixmap(center_x-ic_size/2, y, ic_size, ic_size, ts.getIsLeftLightOn() ? ic_ts_left[1] : ic_ts_left[0]);
    p.drawPixmap(center_x-ic_size/2 + ic_size + ic_gap, y, ic_size, ic_size, ts.getIsGreenLightOn() ? ic_ts_green[1] : ic_ts_green[0]);

    p.setFont(InterFont(text_size, QFont::Bold));

    if(ts.getRedLightRemainTime() > 0) {
      QColor color = QColor(252, 45, 50, 230);
      drawRoundedText(p, center_x - ic_size - ic_gap, y + ic_size + text_h_margin + 20, QString::number(ts.getRedLightRemainTime()),
          color, backgroundColor, 20);
    }

    if(ts.getLeftLightRemainTime() > 0) {
      QColor color = QColor(34, 195, 53, 230);
      drawRoundedText(p, center_x, y + ic_size + text_h_margin + 20, QString::number(ts.getLeftLightRemainTime()),
          color, backgroundColor, 20);
    }

    if(ts.getGreenLightRemainTime() > 0) {
      QColor color = QColor(34, 195, 53, 230);
      drawRoundedText(p, center_x + ic_size + ic_gap, y + ic_size + text_h_margin + 20, QString::number(ts.getGreenLightRemainTime()),
          color, backgroundColor, 20);
    }

    p.restore();
    return true;
  }
  return false;
}
